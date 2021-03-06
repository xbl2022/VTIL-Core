// Copyright (c) 2020 Can Boluk and contributors of the VTIL Project   
// All rights reserved.   
//    
// Redistribution and use in source and binary forms, with or without   
// modification, are permitted provided that the following conditions are met: 
//    
// 1. Redistributions of source code must retain the above copyright notice,   
//    this list of conditions and the following disclaimer.   
// 2. Redistributions in binary form must reproduce the above copyright   
//    notice, this list of conditions and the following disclaimer in the   
//    documentation and/or other materials provided with the distribution.   
// 3. Neither the name of VTIL Project nor the names of its contributors
//    may be used to endorse or promote products derived from this software 
//    without specific prior written permission.   
//    
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE   
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE  
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE   
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR   
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF   
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS   
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN   
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)   
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE  
// POSSIBILITY OF SUCH DAMAGE.        
//
#pragma once
#include <vtil/utility>
#include <mutex>
#include <functional>
#include <unordered_map>
#include "../arch/identifier.hpp"
#include "instruction.hpp"
#include "call_convention.hpp"

namespace vtil
{
	// Forward declaration of basic block.
	//
	struct basic_block;

	// Type we describe timestamps in.
	//
	using epoch_t = uint64_t;
	static constexpr epoch_t invalid_epoch = ~0;

	// Declare types of path containers.
	//
	using path_set = std::unordered_set<const basic_block*, hasher<>>;
	using path_map = std::unordered_map<
		const basic_block*, 
		std::unordered_map<const basic_block*, path_set, hasher<>>, 
		hasher<>
	>;

	// Descriptor for any routine that is being translated.
	//
	struct routine
	{
	protected:
		// This structure cannot be copied without a call to ::clone().
		//
		routine( const routine& ) = default;
		routine& operator=( const routine& ) = default;
	public:
		// Mutex guarding the whole structure, more information on thread-safety can be found at basic_block.hpp.
		//
		mutable relaxed<std::recursive_mutex> mutex;

		// Physical architecture routine is bound to.
		//
		architecture_identifier arch_id;

		// Cache of explored blocks, mapping virtual instruction pointer to the basic block structure.
		//
		std::unordered_map<vip_t, basic_block*> explored_blocks;

		// Cache of paths from block A to block B.
		//
		path_map path_cache;

		// Reference to the first block, entry point.
		// - Can be accessed without acquiring the mutex as it will be assigned strictly once.
		//
		basic_block* entry_point = nullptr;

		// Last local identifier used for an internal register.
		//
		relaxed_atomic<uint64_t> last_internal_id = { 0 };

		// Calling convention of the routine.
		//
		call_convention routine_convention;

		// Calling convention of a non-specialized VXCALL.
		//
		call_convention subroutine_convention;

		// Convention of specialized calls, maps the vip of the VXCALL instruction onto the convention used.
		//
		std::unordered_map<vip_t, call_convention> spec_subroutine_conventions;

		// Misc. stats.
		//
		relaxed_atomic<uint64_t> local_opt_count = { 0 };

		// Multivariate runtime context.
		//
		multivariate<routine> context = {};
		
		// Cache of depth ordered lists.
		//
		struct depth_placement
		{
			size_t level_dependency;
			size_t level_depth;
			const basic_block* block;
		};
		struct depth_ordered_list
		{
			epoch_t epoch = invalid_epoch;
			std::vector<routine::depth_placement> list;
		};
		mutable depth_ordered_list depth_ordered_list_cache[ 2 ];

		// Epoch provided to allow external entities determine if the routine 
		// is modified or not since their last read from it in an easy and fast way.
		//
		epoch_t cfg_epoch;
		relaxed_atomic<epoch_t> epoch;
		void signal_modification() { ++epoch; }
		void signal_cfg_modification() { ++epoch; ++cfg_epoch; }

		// Constructed from architecture identifier.
		//
		routine( architecture_identifier arch_id ) 
			: arch_id( arch_id ), epoch( make_random<epoch_t>() ), cfg_epoch( make_random<epoch_t>() )
		{
			switch ( arch_id )
			{

#if _M_X64 || __x86_64__
				case architecture_amd64:   routine_convention =    amd64::default_call_convention;
				                           subroutine_convention = amd64::default_call_convention; break;
#elif _M_IX86 || __i386__
				case architecture_x86:   routine_convention =      x86::default_call_convention;
					                       subroutine_convention = x86::default_call_convention; break;
#elif _M_ARM64 || __aarch64__
			case architecture_arm64:   routine_convention =    arm64::default_call_convention;
			                           subroutine_convention = arm64::default_call_convention; break;
#endif

				case architecture_virtual: routine_convention =    { .purge_stack = true };
				                           subroutine_convention = { .purge_stack = true }; break;
			}
		};

		// Wrap around explored blocks list, thread-safety left to caller.
		//
		auto begin() { return explored_blocks.begin(); }
		auto end()   { return explored_blocks.end(); }
		auto begin() const { return explored_blocks.cbegin(); }
		auto end() const   { return explored_blocks.cend(); }
		auto size() const  { return explored_blocks.size(); }

		// Helpers for the allocation of unique internal registers.
		//
		register_desc alloc( bitcnt_t size )
		{
			return { register_internal, last_internal_id++, size };
		}
		template<typename... params>
		auto alloc( bitcnt_t size_0, params... size_n )
		{
			return std::make_tuple( alloc( size_0 ), alloc( size_n )... );
		}

		// Invokes the enumerator passed for each basic block this routine contains.
		//
		template<typename T>
		void for_each( T&& fn )
		{
			std::lock_guard _g( mutex );
			for ( auto& [vip, block] : explored_blocks )
				if ( enumerator::invoke( fn, block ).should_break )
					return;
		}

		// Gets the calling convention for the given VIP (that resolves into VXCALL.
		//
		call_convention get_cconv( vip_t vip ) const
		{
			std::lock_guard _g( mutex );
			if ( auto it = spec_subroutine_conventions.find( vip ); it != spec_subroutine_conventions.end() )
				return it->second;
			return subroutine_convention;
		}

		// Sets the calling convention for the given VIP (that resolves into VXCALL.
		//
		void set_cconv( vip_t vip, const call_convention& cc )
		{
			std::lock_guard _g( mutex );
			spec_subroutine_conventions[ vip ] = cc;
		}

		// Gets path from src to dst.
		//
		const path_set& get_path( const basic_block* src, const basic_block* dst ) const;

		// Simple helpers to check if path from src to dst exists.
		//
		bool has_path( const basic_block* src, const basic_block* dst ) const;

		// Checks whether the block is in a loop.
		//
		bool is_looping( const basic_block* blk ) const;

		// Explores the paths for the block, reserved for internal use.
		//
		void explore_paths( const basic_block* blk );

		// Flushes the path cache, reserved for internal use.
		//
		void flush_paths();

		// Finds a block in the list, get variant will throw if none found.
		//
		basic_block* find_block( vip_t vip ) const;
		basic_block* get_block( vip_t vip ) const;

		// Tries creating a new block bound to this routine.
		// - Mimics ::emplace, returns an additional bool reporting whether it's found or not.
		//
		std::pair<basic_block*, bool> create_block( vip_t vip, basic_block* src = nullptr );

		// Deletes a block, should have no links or links must be nullified (no back-links).
		//
		void delete_block( basic_block* block );

		// Enumerates every instruction in the routine forward/backward, within the boundaries if specified.
		// -- @ routine_helpers.hpp
		//
		template<typename callback, typename iterator_type>
		void enumerate( callback fn, const iterator_type& src, const iterator_type& dst = {} ) const;
		template<typename callback, typename iterator_type>
		void enumerate_bwd( callback fn, const iterator_type& src, const iterator_type& dst = {} ) const;

		// Gets a list of exits.
		//
		std::vector<const basic_block*> get_exits() const;

		// Gets a list of depth ordered block lists that can be analysed in parallel with 
		// weakened dependencies on previous level.
		//
		std::vector<depth_placement> get_depth_ordered_list( bool fwd ) const;

		// Provide basic statistics about the complexity of the routine.
		//
		size_t num_blocks() const;
		size_t num_instructions() const;
		size_t num_branches() const;

		// Routine structures free all basic blocks they own upon their destruction.
		//
		~routine();

		// Clones the routine and it's every block.
		//
		routine* clone() const;
	};
};
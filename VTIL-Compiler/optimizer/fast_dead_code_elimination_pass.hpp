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
#include <vtil/arch>
#include "../common/interface.hpp"

namespace vtil::optimizer
{
	struct register_id
	{
		uint64_t combined_id;
		uint64_t flags;

		bool operator==( const register_id &other ) const
		{
			return other.combined_id == combined_id && other.flags == flags;
		}

		bool operator!=( const register_id &other ) const
		{
			return other.combined_id != combined_id || other.flags != flags;
		}

		explicit register_id( vtil::register_desc reg ) : combined_id( reg.combined_id ), flags( reg.flags )
		{}
	};
}

namespace std
{
	template<>
	struct hash<vtil::optimizer::register_id>
	{
		size_t operator()( const vtil::optimizer::register_id& id ) const
		{
			return size_t( id.flags << 16u ) | (size_t) id.combined_id;
		}
	};
}

namespace vtil::optimizer
{
	// Removes every non-volatile instruction whose effects are
	// ignored or overwritten.
	//
	struct fast_dead_code_elimination_pass : pass_interface<execution_order::custom>
	{
		std::unordered_set< basic_block* > sealed;
		std::unordered_map< basic_block*, std::unordered_map< register_id, uint64_t > > reg_map;

		size_t fast_xblock_dce( basic_block* blk );
		size_t pass( basic_block* blk, bool xblock = false ) { return 0; }
		size_t xpass( routine* rtn ) override
		{
			return fast_xblock_dce( rtn->entry_point );
		}
	};

	struct fast_local_dead_code_elimination_pass : pass_interface<>
	{
		size_t pass( basic_block* blk, bool xblock = false ) override;
	};
};
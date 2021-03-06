/*
 * Copyright (C) 2013  Ta Thanh Dinh <thanhdinh.ta@inria.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include "checkpoint.h"
#include "../analysis/dataflow.h"
#include "../utilities/utils.h"
#include "../main.h"

namespace engine
{

using namespace analysis;
using namespace utilities;

/**
 * @brief a checkpoint is created before the execution of the current examined instruction. 
 * 
 * @param current_context the cpu context (values of registers) at the current execution order
 */
checkpoint::checkpoint(CONTEXT* current_context)
{
  // store the current cpu context,
  PIN_SaveContext(current_context, &(this->cpu_context));
  
  // and the current memory state
  boost::unordered_set<ptr_insoperand_t>::iterator operand_iter;
  ADDRINT mem_addr;
  
  for (operand_iter = outerface_at_execorder[current_execorder].begin(); 
       operand_iter != outerface_at_execorder[current_execorder].end(); ++operand_iter) 
  {
    // verify if the operand is a memory address
    if ((*operand_iter)->value.type() == typeid(ADDRINT)) 
    {
      // store the current memory at this address
      mem_addr = boost::get<ADDRINT>((*operand_iter)->value);
      this->memory_state_at[mem_addr] = *(reinterpret_cast<UINT8*>(mem_addr));
    }
  }
}


/**
 * @brief the checkpoint stores the original values at memory addresses before the executed 
 * instruction overwrites these values. Note that with the new move_backward approach, this logging
 * may not be necessary anymore (and then the performance of resolving-state is much improved).
 * 
 * @param memory_written_address the beginning address that will be written
 * @param memory_written_length the length of written addresses
 * @return void
 */
void checkpoint::log_before_execution(ADDRINT memory_written_address, UINT8 memory_written_length)
{
	ADDRINT mem_addr;
  ADDRINT upper_bound_address = memory_written_address + memory_written_length;
  
  for (mem_addr = memory_written_address; mem_addr < upper_bound_address; ++mem_addr) 
  {
    // log the original value at this written address
    if (this->memory_change_log.find(mem_addr) == this->memory_change_log.end()) 
    {
      this->memory_change_log[mem_addr] = *(reinterpret_cast<UINT8*>(mem_addr));
    }
  }
  
  return;
}


/**
 * @brief modify randomly memory values (at the input buffer) which read at the checkpoint.
 * 
 * @return void
 */
void checkpoint::modify_input()
{
  boost::unordered_set<ADDRINT>::iterator mem_addr_iter;
  for (mem_addr_iter = this->memory_addresses_to_modify.begin(); 
       mem_addr_iter != this->memory_addresses_to_modify.end(); ++mem_addr_iter) 
  {
    *(reinterpret_cast<UINT8*>(*mem_addr_iter)) = utils::random_uint8();
  }
  return;
}


/**
 * @brief restore original memory values (at the input buffer) which read at the checkpoint.
 * 
 * @return void
 */
void checkpoint::restore_input()
{
  boost::unordered_set<ADDRINT>::iterator mem_addr_iter;
  for (mem_addr_iter = this->memory_addresses_to_modify.begin(); 
       mem_addr_iter != this->memory_addresses_to_modify.end(); ++mem_addr_iter) 
  {
    *(reinterpret_cast<UINT8*>(*mem_addr_iter)) = original_msgstate_at_address[*mem_addr_iter];
  }
  return;
}



} // end of engine namespace

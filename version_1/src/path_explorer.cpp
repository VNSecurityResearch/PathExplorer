#include "operation/instrumentation.h"
#include "operation/tainting_phase.h"
#include "operation/capturing_phase.h"
#include "common.h"
#include "util/stuffs.h"
#include <ctime>

/* ---------------------------------------------------------------------------------------------- */
/*                                        global variables                                        */
/* ---------------------------------------------------------------------------------------------- */
addr_ins_map_t          ins_at_addr;  // statically examined instructions
order_ins_map_t         ins_at_order; // dynamically examined instructions

UINT32                  total_rollback_times;
UINT32                  local_rollback_times;
UINT32                  trace_size;

UINT32                  max_total_rollback_times;
UINT32                  max_local_rollback_times;
UINT32                  max_trace_size;

ptr_checkpoints_t       saved_checkpoints;

ptr_cond_direct_inss_t  detected_input_dep_cfis;
ptr_cond_direct_ins_t   exploring_cfi;

UINT32                  current_exec_order;
path_code_t             current_path_code;
ptr_explorer_graph_t    explored_fsa;

ptr_exec_dfa_t          abstracted_dfa;

ptr_exec_path_t         current_exec_path;
ptr_exec_paths_t        explored_exec_paths;

ADDRINT                 received_msg_addr;
UINT32                  received_msg_size;
UINT32                  received_msg_order;
bool                    interested_msg_is_received;
ptr_uint8_t             fresh_input;

INT                     process_id;
std::string             process_id_str;

THREADID                traced_thread_id;
bool                    traced_thread_is_fixed;

#if defined(__gnu_linux__)
ADDRINT                 logged_syscall_index;   // logged syscall index
ADDRINT                 logged_syscall_args[6]; // logged syscall arguments
#endif

running_phase           current_running_phase;

UINT64                  executed_ins_number;
UINT64                  econed_ins_number;

time_t                  start_time;
decltype(start_time)    stop_time;

std::ofstream           log_file;

ptr_random_engine_t     ptr_rand_engine;

/* ---------------------------------------------------------------------------------------------- */
/*                                         input handler functions                                */
/* ---------------------------------------------------------------------------------------------- */
KNOB<UINT32> max_local_rollback_knob       (KNOB_MODE_WRITEONCE, "pintool", "r", "7000",
                                            "specify the maximum local number of rollback" );

KNOB<UINT32> max_total_rollback_knob       (KNOB_MODE_WRITEONCE, "pintool", "t", "90000",
                                            "specify the maximum total number of rollback" );

KNOB<UINT32> max_trace_length_knob         (KNOB_MODE_WRITEONCE, "pintool", "l", "100",
                                            "specify the length of the longest trace" );

KNOB<UINT32> interested_input_order_knob   (KNOB_MODE_WRITEONCE, "pintool", "i", "1",
                                            "specify the order of the treated input");

/* ---------------------------------------------------------------------------------------------- */
/*                                  basic instrumentation functions                               */
/* ---------------------------------------------------------------------------------------------- */
/**
 * @brief initialize input variables
 */
auto start_exploring (VOID *data) -> VOID
{
  max_trace_size            = max_trace_length_knob.Value();
  trace_size                = 0;
  current_exec_order        = 0;

  total_rollback_times      = 0;
  local_rollback_times      = 0;

  max_total_rollback_times  = max_total_rollback_knob.Value();
  max_local_rollback_times  = max_local_rollback_knob.Value();
  received_msg_order        = interested_input_order_knob.Value();
  
  executed_ins_number       = 0;
  econed_ins_number         = 0;
  
#if defined(__gnu_linux__)
  logged_syscall_index      = syscall_inexist;
#endif

  explored_fsa              = explorer_graph::instance();
  abstracted_dfa            = execution_dfa::instance();

  exploring_cfi.reset();
  traced_thread_is_fixed    = false;

  process_id                = PIN_GetPid();
  process_id_str            = std::to_string(static_cast<long long>(process_id));

//  reopen_console();

  log_file.open(process_id_str + "_path_explorer.log", std::ofstream::out | std::ofstream::trunc);
  if (!log_file) PIN_ExitProcess(1);

  tfm::format(log_file, "total rollback %d, local rollback %d, trace depth %d, ",
              max_total_rollback_times, max_local_rollback_times, max_trace_size);

#if !defined(ENABLE_FAST_ROLLBACK)
  tfm::format(log_file, "fast rollback disabled, ");
#else
  tfm::format(log_file, "fast rollback enabled, ");
#endif

#if !defined(DISABLE_FSA)
  tfm::format(log_file, "FSA reconstruction enabled\n");
#elif
  tfm::format(log_file, "FSA reconstruction disabled\n");
#endif
  tfm::format(log_file, "======================================================================\n");
//  log_file << "=================================================================================\n";

  instrumentation::initialize();

//  start_time = std::time(0); std::srand(static_cast<unsigned int>(start_time));
//  ptr_rand_engine = std::make_shared<std::default_random_engine>(std::random_device()());

  return;
}


/**
 * @brief collect explored results
 */
auto stop_exploring (INT32 code, VOID *data) -> VOID
{
  stop_time = std::time(0);
  tfm::format(std::cerr, "%d seconds elapsed for generating DFA\n", stop_time - start_time);
  start_time = stop_time;

//  tfm::format(std::cerr, "calculating results\n");

//  tfm::format(std::cerr, "saving static trace\n");
//  save_static_trace(process_id_str + "_path_explorer_static_trace.log");

//  tfm::format(std::cerr, "save dynamic trace\n");
//  save_explored_trace("dynamic_trace_" + process_id_str + ".log");

//  tfm::format(std::cerr, "save received message\n");
//  save_received_message("message_" + process_id_str + ".log");

#if !defined(DISABLE_FSA)
  tfm::format(std::cerr, "extracting CFI tree\n");
  explored_fsa->extract_cfi_tree();

//  tfm::format(std::cerr, "saving CFI inputs\n");
//  save_cfi_inputs(process_id_str + "_cfi_inputs.log");

  tfm::format(std::cerr, "saving all trees\n");
  explored_fsa->save_to_file(process_id_str + "_path_explorer_explored_fsa.dot");
#endif

  UINT32 resolved_cfi_num = 0, singular_cfi_num = 0;
  std::for_each(detected_input_dep_cfis.begin(), detected_input_dep_cfis.end(),
                [&](ptr_cond_direct_ins_t cfi)
  {
    if (cfi->is_resolved) resolved_cfi_num++;
    if (cfi->is_singular) singular_cfi_num++;
  });

  tfm::format(log_file, "%d seconds elapsed, %d rollbacks used, %d/%d/%d resolved/singular/total CFI.\n",
              (stop_time - start_time), total_rollback_times, resolved_cfi_num, singular_cfi_num,
              detected_input_dep_cfis.size());
  log_file.close();

  calculate_exec_path_conditions(explored_exec_paths);

//  show_cfi_logged_inputs();
  tfm::format(std::cerr, "constructing raw DFA\n");
  abstracted_dfa->add_exec_paths(explored_exec_paths);

  tfm::format(std::cerr, "pre-processing some states\n");
  abstracted_dfa->pre_processing();

  tfm::format(std::cerr, "saving raw DFA to file\n");
  abstracted_dfa->save_to_file("raw_" + process_id_str + ".dot");

//  tfm::format(std::cerr, "optimizing raw DFA\n");
//  abstracted_dfa->optimize();
//  abstracted_dfa->co_optimize();

//  tfm::format(std::cerr, "saving optimized DFA to file\n");
//  abstracted_dfa->save_to_file("optimized_" + process_id_str + ".dot");

  tfm::format(std::cerr, "abstracting DFA\n");
//  abstracted_dfa->approximate();
  abstracted_dfa->co_approximate();

  tfm::format(std::cerr, "saving abstracted DFA to file\n");
  abstracted_dfa->save_to_file("abstracted_" + process_id_str + ".dot");

  stop_time = std::time(0);
  tfm::format(std::cerr, "%d seconds elapsed for abstracting DFA\n", stop_time - start_time);

  return;
}


/* ---------------------------------------------------------------------------------------------- */
/*                                          main function                                         */
/* ---------------------------------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
  tfm::format(std::cerr, "initialize image symbol tables\n"); PIN_InitSymbols();

  tfm::format(std::cerr, "initialize Pin\n");
  if (PIN_Init(argc, argv))
  {
    tfm::format(std::cerr, "Pin initialization failed\n");
    log_file.close(); PIN_ExitProcess(1);
  }
  else
  {
    tfm::format(std::cerr, "Pin initialization success\n");

    tfm::format(std::cerr, "activate Pintool data-initialization\n");
    PIN_AddApplicationStartFunction(start_exploring, 0);  // 0 is the (unused) input data

    tfm::format(std::cerr, "activate image-loading instrumenter\n");
    IMG_AddInstrumentFunction(instrumentation::image_loading, 0);

//    tfm::format(std::cerr, "activate routine-calling instrumenters\n"); (never active this!!!)
//    RTN_AddInstrumentFunction(instrumentation::routine_calling, 0);

    tfm::format(std::cerr, "activate instruction-executing instrumenters\n");
    INS_AddInstrumentFunction(instrumentation::instruction_executing, 0);

    tfm::format(std::cerr, "activate process-creating instrumenter\n");
    PIN_AddFollowChildProcessFunction(instrumentation::process_creating, 0);

    // In Windows environment, the input tracing is through socket api instead of system call
#if defined(__gnu_linux__)
    PIN_AddSyscallEntryFunction(capturing::syscall_entry_analyzer, 0);
    PIN_AddSyscallExitFunction(capturing::syscall_exit_analyzer, 0);
#endif

    tfm::format(std::cerr, "activate Pintool data-finalization\n");
    PIN_AddFiniFunction(stop_exploring, 0);

//    log_file.flush();

    // now the control is passed to pin, so the main function will never return
    PIN_StartProgram();
  }

  // in fact, it is reached only if the Pin initialization fails
  return 0;
}

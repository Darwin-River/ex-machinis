/******************************************************************************

  PROJECT    : Ex-Machinis

  DESCRIPTION: Virtual Machine management module
 
******************************************************************************/

/******************************* INCLUDES ************************************/

#include <string.h>
#include <assert.h>
#include <time.h>

#include "vm.h"
#include "engine.h"
#include "trace.h"
#include "vm_extension.h"
#include "db.h"

/******************************* DEFINES *************************************/

/******************************* TYPES ***************************************/



/******************************* PROTOTYPES **********************************/



/******************************* GLOBAL VARIABLES ****************************/

// we store in global buffer the last command output and use pointer to next char
char* g_output_buffer = NULL;
char* g_last_command_output = NULL;
size_t g_current_size = 0;

// To yield a VM we control time elapsed since we started it, and limit this time
time_t g_vm_start_time = 0;

// Last VM is yield
int g_vm_yield = 0;

/******************************* LOCAL FUNCTIONS *****************************/

/** ****************************************************************************

  @brief      Callback invoked by VM when we need to write a character

  @param[in]  ch Char received from VM
  @param[in]  file, handle needed to write to a source, if needed

  @return     ch on success, negative on failure

*******************************************************************************/
int vm_putc_cb(int ch, void *file) 
{
  //engine_trace(TRACE_LEVEL_DEBUG, "Char [%c] received from VM output", ch);

  if(g_output_buffer)
  {
    int next_pos = strlen(g_output_buffer);

    if(next_pos > g_current_size) {
      //engine_trace(TRACE_LEVEL_ALWAYS, "WARNING: Max out buffer size [%ld] reached (discarded)", g_current_size);
      return ch;
    }

    g_last_command_output = (g_output_buffer + next_pos);
    *g_last_command_output = ch;
  }

  return ch;
}

/** ****************************************************************************

  @brief      Callback invoked by VM to check if a given VM must stop or continue

  @param[in]  Yield parameter

  @return     0 continue, 1 yield

*******************************************************************************/
int vm_yield_cb(void* param) 
{
  time_t now = time(NULL);
  int max_time = engine_get_max_cycle_seconds();

  if((now - g_vm_start_time) >= max_time) {
    engine_trace(TRACE_LEVEL_ALWAYS, 
      "WARNING: VM must yield, elapsed more than [%d] seconds(%ld - %ld), current output buffer [%s]", 
      max_time, now, g_vm_start_time, g_output_buffer?g_output_buffer:"NULL");

    g_vm_yield = 1;

    return 1;
  }

  return 0;
}

/** ****************************************************************************

  @brief      Adds all user defined callbacks to the VM (defining new words with 'embed_eval')

  @param[in]  vm       Current VM
  @param[in]  optimize Flag to optimize or not the word definition in memory 
  @param[in]  cb       Array of callbacks to be added
  @param[in]  number   Callbacks array size

  @return     Execution result

*******************************************************************************/
static int vm_add_callbacks(VirtualMachine_t* const vm, const bool optimize,  VmExtensionCb_t *cb, const size_t number) 
{
  // input pointers checked by calling function
  //assert(vm && cb);

  const char *optimizer = optimize ? "-2 cells allot ' vm chars ," : "";
  static const char *preamble = "only forth definitions system +order\n";
  int r = 0;

  if((r = embed_eval(vm, preamble)) < 0) {
    engine_trace(TRACE_LEVEL_ALWAYS, "ERROR: evaluation of preamble (%s) returned %d", preamble, r); 
    return r;
  }

  for(size_t i = 0; i < number; i++) {
    char line[80] = { 0 };
    if(!cb[i].use)
      continue;
    r = snprintf(line, sizeof(line), ": %s %u vm ; %s\n", cb[i].name, (unsigned)i, optimizer);
    assert(strlen(line) < sizeof(line) - 1);
    if(r < 0) {
      engine_trace(TRACE_LEVEL_ALWAYS, "ERROR: format error in snprintf (returned %d)", r); 
      return -1;
    }
    if((r = embed_eval(vm, line)) < 0) {
      engine_trace(TRACE_LEVEL_ALWAYS, "ERROR: evaluation of statement (%s) returned %d", line, r); 
      return r;
    }

    engine_trace(TRACE_LEVEL_ALWAYS, "Callback (%s) added to VM", cb[i].name); 
  }
  embed_reset(vm);
  return 0;
}

/******************************* PUBLIC FUNCTIONS *****************************/

/** ****************************************************************************

  @brief      Creates a new FORTH VM

  @param[in]  Agent ID for this VM

  @return     New VM object created or NULL when failed

*******************************************************************************/
VirtualMachine_t* vm_new(int agent_id)
{
    VirtualMachine_t* vm = NULL;

    // Allocate new embedded VM
    vm = (VirtualMachine_t*)embed_new();
    const char* imageFile =  engine_get_forth_image_path();

    if(vm && imageFile)
    {
        if(embed_load(vm, imageFile) != 0) {
            engine_trace(TRACE_LEVEL_ALWAYS, 
                "Unable to create VM using base file [%s]",
                imageFile);

            vm_free(vm);
            return NULL;
        }

        engine_trace(TRACE_LEVEL_ALWAYS, 
            "New VM created with size [%ld] and core size [%ld] with file [%s] for agent [%d]",
        	sizeof(*vm), sizeof(*(vm->m)), imageFile, agent_id); 

        embed_opt_t vm_default_options = *embed_opt_get(vm);
        embed_opt_t vm_engine_options = vm_default_options;
        vm_engine_options.put = vm_putc_cb;
        vm_engine_options.yield = vm_yield_cb;
        vm_engine_options.options |= EMBED_VM_QUITE_ON;
        //embed_opt_set(vm, &vm_engine_options);

        // We need here to add VM extensions, the very first time we create an VM
        VmExtension_t* vm_ext = vm_extension_new();

        if(!vm_ext) 
        {
          engine_trace(TRACE_LEVEL_ALWAYS, "ERROR: unable to allocate VM extension"); 
          vm_free(vm);
          vm = NULL;
        }
        else
        {
          engine_trace(TRACE_LEVEL_ALWAYS, 
            "VM extension created at [%lp]", 
            vm_ext);

          // Join vm and its ext
          vm_ext->h = vm;

          // Use options defined by extension
          vm_engine_options.callback = vm_ext->o.callback;
          vm_engine_options.param = vm_ext;
          embed_opt_set(vm, &vm_engine_options);

          // control time each VM spends, we need to initialize yield to run some commands
          g_vm_start_time = time(NULL);
          g_vm_yield = 0;

          // Add all callbacks
          if(vm_add_callbacks(vm, true, vm_ext->callbacks, vm_ext->callbacks_length) < 0) 
          {
            engine_trace(TRACE_LEVEL_ALWAYS, "ERROR: unable to add VM callbacks"); 
            vm_free(vm);
            vm = NULL;
          }
        }
    }      

    return vm;
}

/** ****************************************************************************

  @brief      Deallocates FORTH VM resources

  @param[in]  vm Virtual Machine object we want to remove

  @return     void

*******************************************************************************/
void vm_free(VirtualMachine_t* vm)
{ 
    if(vm)
    {    
        engine_trace(TRACE_LEVEL_ALWAYS, "VM deallocated"); 

        embed_free((forth_t*)vm);
        vm = NULL;
        g_vm_yield = 0;
    }
    else
    {
        engine_trace(TRACE_LEVEL_ALWAYS, "WARNING: Unable to free NULL VM"); 
    }        
}

/** ****************************************************************************

  @brief      Creates a new FORTH VM using input bytes

  @param[in]  agent_id Current agent ID
  @param[in]  vm_bytes Bytes to be used to build a new VM
  @param[in]  size     Bytes number

  @return     New VM object created or NULL when failed

*******************************************************************************/
VirtualMachine_t* vm_from_bytes(int agent_id, char* vm_bytes, size_t size)
{
    VirtualMachine_t* vm = NULL;
    char dump_file[PATH_MAX];
    int error = 0;

#ifdef USE_OLD_EMBED
    if(vm_bytes)
    {
        vm = embed_load_from_memory((unsigned char*)vm_bytes, size);

        if(vm) {
            engine_trace(TRACE_LEVEL_ALWAYS, 
                "VM created from [%ld] bytes of memory for agent", 
                size);             
        } else {
            engine_trace(TRACE_LEVEL_ALWAYS, 
                "ERROR: Unable to create VM from [%ld] bytes of memory for agent", 
                size); 
        }
    }    
#else  // USE latest embed version
    if(vm_bytes)
    {
        // Create a new VM and load its core from DB
        vm = embed_new();

        if(vm)
        {         
          // Define output options to retrieve the buffer
          embed_opt_t vm_default_options = *embed_opt_get(vm);
          embed_opt_t vm_engine_options = vm_default_options;
          vm_engine_options.put = vm_putc_cb;
          vm_engine_options.yield = vm_yield_cb;
          vm_engine_options.options |= EMBED_VM_QUITE_ON;

          // We need here to add VM extensions again
          VmExtension_t* vm_ext = vm_extension_new();

          if(!vm_ext) 
          {
            engine_trace(TRACE_LEVEL_ALWAYS, "ERROR: unable to allocate VM extension"); 
            vm_free(vm);
            vm = NULL;
          }
          else
          {
            engine_trace(TRACE_LEVEL_ALWAYS, 
              "VM extension created at [%lp]", 
              vm_ext);

            // Join vm and its ext
            vm_ext->h = vm;

            // Use options defined by extension
            vm_engine_options.callback = vm_ext->o.callback;
            vm_engine_options.param = vm_ext;
            embed_opt_set(vm, &vm_engine_options);

            // control time each VM spends, we need to initialize yield to run some commands
            g_vm_start_time = time(NULL);
            g_vm_yield = 0;

            // Add all callbacks
            if(vm_add_callbacks(vm, true, vm_ext->callbacks, vm_ext->callbacks_length) < 0) 
            {
              engine_trace(TRACE_LEVEL_ALWAYS, "ERROR: unable to add VM callbacks"); 
              vm_free(vm);
              vm = NULL;
            }
          }
        }

        if(vm && !error) 
        {
            // Load VM core from memory now
            error = embed_load_buffer(vm, (const uint8_t *)vm_bytes, size);

            if(!error) {
                engine_trace(TRACE_LEVEL_ALWAYS,
                    "VM created from [%ld] bytes of memory for agent",
                    size);

                // When enabled, we keep track of each VM content on disk
                if(engine_get_dump_vm_flag()) {
                    sprintf(dump_file, "%s/vm_agent_%d.dump", 
                        engine_get_dump_vm_path(),
                        agent_id);

                    engine_trace(TRACE_LEVEL_ALWAYS,
                        "Saving VM for agent [%d] into file [%s]",
                        agent_id,
                        dump_file);

                    embed_save(vm, dump_file);
                }
            } else {
                engine_trace(TRACE_LEVEL_ALWAYS,
                    "ERROR: Unable to create VM from [%ld] bytes of memory for agent",
                    size);
            }
        }
    }
#endif  

    return vm;
}

/** ****************************************************************************

  @brief      Runs engine logic loop

  @param[in]  vm         VM object
  @param[in]  command    Command to be executed in current VM
  @param[out] out_buffer Output buffer
  @param[int] out_size   Output buffer size

  @return     Execution result code (ErrorCode_t)

*******************************************************************************/
ErrorCode_t vm_run_command(VirtualMachine_t* vm, Command_t* command, char* out_buffer, size_t out_size)
{
    ErrorCode_t result = ENGINE_OK;

    if(vm && command)
    {
        // Check if it is an empty = 'abort' command
        if(strlen((const char*)command->code)) 
        {    
            engine_trace(TRACE_LEVEL_ALWAYS, "Running command: [%s]", command->code);

            g_output_buffer = out_buffer;
            g_last_command_output = g_output_buffer;
            g_current_size = out_size;
            memset(g_output_buffer, 0, g_current_size); 

            // Check if it is a resume / execute command and also if there are pending commands running
            int forth_result = -1;

            if(strcmp(command->code, engine_get_vm_resume_command()))
            { 
                // it is normal command
                engine_trace(TRACE_LEVEL_ALWAYS, "Executing command at VM: [%s]", command->code);

                forth_result = embed_eval((forth_t*)vm, (const char*)command->code);

            }
            else
            {
                // Just resume VM and execute pending codes
                engine_trace(TRACE_LEVEL_ALWAYS, "VM resumed: [%s]", command->code);

                forth_result = embed_vm((forth_t*)vm);
            }

            if(forth_result < 0)
            {
                engine_trace(TRACE_LEVEL_ALWAYS, 
                    "ERROR: Command evaluation failed for [%s] result [%d]", command->code, forth_result);

                result = ENGINE_FORTH_EVAL_ERROR;
            }
            else
            {
                engine_trace(TRACE_LEVEL_ALWAYS, 
                    "Command succesfully evaluated: [%s] result [%s]", command->code, out_buffer);
            }
        }
        else 
        {
            // ABORT COMMAND
            engine_trace(TRACE_LEVEL_ALWAYS, "ABORT command received");

            // Notify by email
            engine_vm_output_cb("Script aborted");

            // reset VM stuff
            embed_reset((forth_t*)vm);
        }
    }
    else
    {
        engine_trace(TRACE_LEVEL_ALWAYS, "ERROR: Could not evaluate command (NULL input)");
        result = ENGINE_INTERNAL_ERROR;
    }      

    return result;
}

/** ****************************************************************************

  @brief      Serializes a given VM objet into a new array of bytes
              (Important: memory returned by this function must be deallocated once used)

  @param[in]      vm       Current VM object
  @param[in|out]  vm_size  Output buffer where we store size of serialized buffer

  @return     Serialized buffer (dynamically allocated) or NULL when error

*******************************************************************************/
char* vm_to_bytes(VirtualMachine_t* vm, size_t* vm_size)
{
    char* vm_bytes = NULL;

    if(vm && vm_size)
    {
        vm_bytes = (char*)embed_save_into_memory((forth_t*) vm, vm_size);

        if(vm_bytes && *vm_size)
        {
            engine_trace(TRACE_LEVEL_ALWAYS, 
                "VM succesfully serialized into [%ld] bytes", *vm_size);

            // Deallocate the extension if any
            if(vm->o.param)
            {
                engine_free(vm->o.param, sizeof(VmExtension_t));
                vm->o.param = NULL;
            }
        }
        else
        {
            engine_trace(TRACE_LEVEL_ALWAYS, 
                "ERROR: Could not serialize VM (serialization failed)");
        }
    }
    else
    {
        engine_trace(TRACE_LEVEL_ALWAYS, "ERROR: Could not serialize VM (NULL input)");
    }      

    return vm_bytes;
}

/** ****************************************************************************

  @brief      When invoked, sends email with current VM output buffer content

  @param[in]  vm       Current VM object

  @return     Serialized buffer (dynamically allocated) or NULL when error

*******************************************************************************/
void vm_report(VirtualMachine_t* vm)
{
    if(vm)
    {
        // invoke the engine to deliver the email with current content
        engine_vm_output_cb(g_output_buffer);

        // clean buffer and point to the beginning
        memset(g_output_buffer, 0, g_current_size); 
    }
}

/** ****************************************************************************

  @brief      Function to check if last VM execution was completed or yield

  @param[in]  None

  @return     0/1 value

*******************************************************************************/
int vm_is_yield() 
{
  return g_vm_yield;
}

/** ****************************************************************************

  @brief      Aborts command execution at current VM

  @param[in]  vm       Current VM object

  @return     void

*******************************************************************************/
void vm_abort(VirtualMachine_t* vm)
{
    if(vm)
    {
        // ABORT COMMAND
        engine_trace(TRACE_LEVEL_ALWAYS, "ABORT command received at VM");

        // Notify by email
        engine_vm_output_cb("Executed VM abort");

        // reset VM stuff
        embed_reset((forth_t*)vm); 
    }
}

/** ****************************************************************************

  @brief      Resets VM (stops command execution and destroys it to create a new one)

  @param[in]  vm       Current VM object

  @return     void

*******************************************************************************/
void vm_reset(VirtualMachine_t* vm)
{
    if(vm)
    {
        // ABORT COMMAND
        engine_trace(TRACE_LEVEL_ALWAYS, "RESET command received at VM");

        // Notify by email
        engine_vm_output_cb("Executed VM reset");

        // reset VM stuff
        embed_reset((forth_t*)vm); 
    }
}


/** ****************************************************************************

  @brief      Reads byte from VM memory at given address (offset given in bytes)

  @param[in]      vm        Current VM object
  @param[in]      addr      Offset (in bytes) we read from
  @param[in|out]  outValue  Output byte where we store the value obtained

  @return     Execution result

*******************************************************************************/
ErrorCode_t vm_read_byte(VirtualMachine_t* vm, uint16_t addr, unsigned char* outValue)
{
    ErrorCode_t result = ENGINE_INTERNAL_ERROR;

    if(vm && outValue)
    {
        *outValue = embed_read_byte(vm, addr); 
        result = ENGINE_OK;
    }

    return result;
}


/** ****************************************************************************

  @brief      Writes byte into VM memory at given address (offset given in bytes)

  @param[in]  vm     Current VM object
  @param[in]  addr   Offset (in bytes) we write into
  @param[in]  value  Byte value to write into VM memory

  @return     Execution result

*******************************************************************************/
ErrorCode_t vm_write_byte(VirtualMachine_t* vm, uint16_t addr, unsigned char value)
{
    ErrorCode_t result = ENGINE_INTERNAL_ERROR;

    if(vm)
    {
        embed_write_byte(vm, addr, value); 
        result = ENGINE_OK;
    }

    return result;
}

/** ****************************************************************************

  @brief      Writes an string into VM memory address

  @param[in]  vm     Current VM object
  @param[in]  addr   Offset (in bytes) we write into
  @param[in]  str    String to insert into VM memory

  @return     Execution result

*******************************************************************************/
ErrorCode_t vm_write_string(VirtualMachine_t* vm, uint16_t addr, char* str)
{
    ErrorCode_t result = ENGINE_INTERNAL_ERROR;

    if(vm && str)
    {
        engine_trace(TRACE_LEVEL_ALWAYS, 
            "Writing string [%s] into VM address [%d]",
            str,
            addr);

        size_t len = strlen(str);
        uint16_t next_addr = addr;

        // write len
        embed_write_byte(vm, next_addr++, (unsigned char)len);

        // write string 
        for(int i=0; i < len; i++) {
            embed_write_byte(vm, next_addr++, (unsigned char)str[i]);
        }
        // End the string with a NULL
        embed_write_byte(vm, next_addr++, (unsigned char)0x00);

        result = ENGINE_OK;
    }

    return result;
}

/** ****************************************************************************

  @brief      Writes an string into VM memory address

  @param[in]  vm     Current VM object
  @param[in]  addr   Offset (in bytes) we write into
  @param[in]  value  Integer value to write

  @return     Execution result

*******************************************************************************/
ErrorCode_t vm_write_integer(VirtualMachine_t* vm, uint16_t addr, uint16_t value)
{
    ErrorCode_t result = ENGINE_INTERNAL_ERROR;

    if(vm)
    {
        unsigned char first_byte = (unsigned char)(value & 0xFF);
        unsigned char second_byte = (unsigned char)(value >> 8);

        engine_trace(TRACE_LEVEL_ALWAYS, 
            "Writing integer [%d] into VM address [%d], bytes [%02X][%02X]",
            value,
            addr,
            first_byte, second_byte);

        // write the 2 bytes 
        embed_write_byte(vm, addr,   first_byte);
        embed_write_byte(vm, addr+1, second_byte);

        result = ENGINE_OK;
    }

    return result;
}


/** ****************************************************************************

  @brief      Writes an string into VM memory address

  @param[in]  vm       Current VM object
  @param[in]  addr     Offset (in bytes) we write into
  @param[in]  date_str Date string to insert into VM memory

  @return     Execution result

*******************************************************************************/
ErrorCode_t vm_write_datetime(VirtualMachine_t* vm, uint16_t addr, char* date_str)
{
    ErrorCode_t result = ENGINE_INTERNAL_ERROR;

    if(vm && date_str)
    {
        engine_trace(TRACE_LEVEL_ALWAYS, 
            "Writing date [%s] into VM address [%d]",
            date_str,
            addr);

        // Times will be placed in memory as two 16-bit values. 
        // The first value will count the number of days from 1 January 2000, 
        // The second value will count the number of even seconds (every other second) since midnight. 
        time_t db_date = db_date_to_timestamp(date_str, OBJECTS_TIMESTAMP_FORMAT);
        time_t ref_date = db_date_to_timestamp(JANUARY_1_2000_DATE, OBJECTS_TIMESTAMP_FORMAT);

        uint16_t days_elapsed = (db_date - ref_date) / (24 * 3600);
        uint16_t seconds_elapsed = (db_date - ref_date) % (24 * 3600);
        uint16_t half_seconds_elapsed = (seconds_elapsed / 2);

        engine_trace(TRACE_LEVEL_ALWAYS, 
            "Writing date [%s] into VM address [%d], days elapsed [%d], seconds [%d], half_seconds [%d]",
            date_str,
            addr,
            days_elapsed,
            seconds_elapsed,
            half_seconds_elapsed);

        vm_write_integer(vm, addr, days_elapsed);
        vm_write_integer(vm, addr+2, half_seconds_elapsed);

        result = ENGINE_OK;
    }

    return result;
}

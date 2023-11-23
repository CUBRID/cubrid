import gdb

# Script prints the correspondence between the GDB displayed thread number (as in "Thread ##") and
# the CUBRID specific thread_entry.index by which all CUBRID GDB helper scripts identify threads inside
# the cub_server process:
#   - iterates all threads of the process
#   - switches to each thread
#   - searches up the callstack to find a frame that contains the thread_entry argument
#   - prints details for the frame and the thread_entry index
#
# Usage:
#   - have the python script handy in the PWD where GDB is lauched
#   - at the GDB prompt, issue:
#        source ./<script.py>
#
# Sample output:
# ----
#   Thread 238
#       Func - css_connection_handler_thread(THREAD_ENTRY*, CSS_CONN_ENTRY*) ~984
#       Frame 1
#       thread_entry.index: 145
# ----
# Explanation:
#   - 238 is the thread number GDB assigns
#   - 145 is the internal index of the thread used in the cub_server process


# starting from a sample at:
#   https://stackoverflow.com/questions/16467923/pruning-backtrace-output-with-gdb-script

# loops through all the Thread objects in the process
for thread in gdb.selected_inferior().threads():
    # switch to this thread (aka: thread #)
    thread.switch()       

    # Just execute a raw gdb command
    #gdb.execute('frame')

    # interate frames in the thread starting from the inner-most one
    fr = gdb.newest_frame()
    frame_index = 0
    while fr is not None:
        #print "= val: %s" % gdb.Frame.read_var(fr, "thread_p")
       	#print 'Frame at {} - {}'.format(frame_index, fr.name())

        #frBlock = fr.block()

        func = fr.function()
        if func is not None:
            if (func.name.find('cubthread::entry*') >= 0) or (func.name.find('THREAD_ENTRY*') >= 0):
                fr.select()
                # or
                #gdb.execute('frame {}'.format(frame_index))

                # Exercise: iterate all variables in the function's block
                #blk = fr.block()
                #print dir(blk)
                for symb in blk:
                    #print dir(symb)
                    print '        symb: {}'.format(symb.name)
                    print '        type: {}'.format(symb.type)
                    varVal = symb.value(fr)
                    #print dir(varVal)
                    print '        val : {}'.format(varVal)

                print "Thread %s" % thread.num
                print '    Func - {} ~{}'.format(func.name, func.line)
                print '    Frame {}'.format(frame_index)
                #gdb.lookup_symbol('thread_p', func)

                # the script assumes the thread_entry variable name is 'thread_p'
                # however, not all threads 
                print '    thread_entry.index: {}'.format(gdb.parse_and_eval('thread_p.index'))
                #gdb.execute('print thread_p.index')

                break
            else:
                # TODO: some functions might not supply the thread_entry as a pointer, but as a reference
                pass

        else:
            # funcions that come empty seems to be for code that has no debug info
            #print 'Func - NOOOOOONE'
            pass

        # move up the stack
        fr = gdb.Frame.older(fr)
        frame_index+=1

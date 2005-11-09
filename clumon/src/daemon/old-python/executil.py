
import os, sys
import select


BASH_PATH='/bin/bash'

def execWithCapture(bin, args):
    return execWithCaptureErrorStatus(bin, args)[0]

def execWithCaptureStatus(bin, args):
    res = execWithCaptureErrorStatus(bin, args)
    return res[0], res[2]

def execWithCaptureErrorStatus(bin, args):
    command = 'LANG=C ' + bin
    if len(args) > 0:
        for arg in args[1:]:
            command = command + ' ' + arg
    return __execWithCaptureErrorStatus(BASH_PATH, [BASH_PATH, '-c', command])



# to be moved back into rhpl when time arrives
def __execWithCaptureErrorStatus(command, argv, searchPath = 0, root = '/', stdin = 0, catchfd = 1, catcherrfd = 2, closefd = -1):
    if not os.access (root + command, os.X_OK):
        raise RuntimeError, command + " can not be run"
    
    (read, write) = os.pipe()
    (read_err,write_err) = os.pipe()
    
    childpid = os.fork()
    if (not childpid):
        # child
        if (root and root != '/'): os.chroot (root)
        if isinstance(catchfd, tuple):
            for fd in catchfd:
                os.dup2(write, fd)
        else:
            os.dup2(write, catchfd)
        os.close(write)
        os.close(read)
        
        if isinstance(catcherrfd, tuple):
            for fd in catcherrfd:
                os.dup2(write_err, fd)
        else:
            os.dup2(write_err, catcherrfd)
        os.close(write_err)
        os.close(read_err)
        
        if closefd != -1:
            os.close(closefd)
        
        if stdin:
            os.dup2(stdin, 0)
            os.close(stdin)
        
        if (searchPath):
            os.execvp(command, argv)
        else:
            os.execv(command, argv)
        # will never get here :)
    
    os.close(write)
    os.close(write_err)
    
    rc = ""
    rc_err = ""
    in_list = [read, read_err]
    while len(in_list) != 0:
        i,o,e = select.select(in_list, [], [])
        for fd in i:
            if fd == read:
                s = os.read(read, 1000)
                if s == '':
                    in_list.remove(read)
                rc = rc + s
            if fd == read_err:
                s = os.read(read_err, 1000)
                if s == '':
                    in_list.remove(read_err)
                rc_err = rc_err + s
    
    os.close(read)
    os.close(read_err)
    
    status = -1
    try:
        (pid, status) = os.waitpid(childpid, 0)
    except OSError, (errno, msg):
        print __name__, "waitpid:", msg
    
    if os.WIFEXITED(status):
        status = os.WEXITSTATUS(status)
    else:
        status = -1
    
    return (rc, rc_err, status)

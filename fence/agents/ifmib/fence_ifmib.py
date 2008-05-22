#!/usr/bin/python
# fence_ifmib.py: fabric fencing for RHCS based on setting a network interface
# to admin down.  Intended to be used for iSCSI connections, can be used with
# anything that supports the IF-MIB and SNMP v2c.
#
# Written by Ross Vandegrift <ross@kallisti.us>
# Copyright (C) 2008 Ross Vandegrift
#  This copyrighted material is made available to anyone wishing to use,
#  modify, copy, or redistribute it subject to the terms and conditions
#  of the GNU General Public License v.2.


import os
os.environ['PYSNMP_API_VERSION'] = 'v2'
import sys, getopt, random, socket
from pysnmp import role, v2c, asn1

ifAdminStatus = '.1.3.6.1.2.1.2.2.1.7.'
up = 1
down = 2
testing = 3

def usage():
    line = '\t%s\t%s'
    print ''
    print 'This script fences a node by sending a command via SNMP to set'
    print 'ifAdminStatus to down.  It is designed to kill node access'
    print 'to the shared storage.  It only supports SNMP v2c.'
    print ''
    print 'Usage: fence_ifmib [options]'
    print line % ('-h', '\tPrint usage')
    print line % ('-V', '\tRun verbosely')
    print line % ('-c [private]', 'Write community string to use')
    print line % ('-a [hostname]', 'IP/hostname of SNMP agent')
    print line % ('-i [index]', 'ifIndex entry of the port ')
    print line % ('-o [action]', 'One of down, up, or status')


def vprint(v, s):
    if v:
        print s


def parseargs():
    try:
        opt, arg = getopt.getopt (sys.argv[1:], 'hVc:v:a:i:o:')
    except getopt.GetoptError, e:
        print str (e)
        usage ()
        sys.exit (-1)

    comm = ipaddr = ifindex = option = verbose = None

    for o, a in opt:
        if o == '-h':
            usage ()
            sys.exit (-1)
        if o == '-V':
            verbose = True
        if o == '-c':
            comm = a
        if o == '-a':
            ipaddr = a
        if o == '-i':
            try:
                ifindex = int(a)
            except:
                sys.stderr.write ('fence_ifmib: ifIndex must be an integer\n')
                usage ()
                sys.exit (-1)
        if o == '-o':
            option = a
            if option not in ('on', 'off', 'status'):
                sys.stderr.write ('fence_ifmib: option must be one of on, off, or status\n')
                usage ()
                sys.exit (-1)

    if comm == None or ipaddr == None or ifindex == None \
            or option == None:
        sts.stderr.write ('All args are madatory!\n')
        usage ()
        sys.exit (-1)

    return (comm, ipaddr, ifindex, option, verbose)


def parsestdin():
    params = {}
    for line in sys.stdin:
        val = line.split('=')
        if len (val) == 2:
            params[val[0].strip ()] = val[1].strip ()

    try:
        comm = params['comm']
    except:
        sys.stdout.write ('fence_ifmib: Error reading community string\n')
        sys.exit (1)

    try:
        ipaddr = params['ipaddr']
    except:
        sys.stdout.write ('fence_ifmib: Error reading destination IP/host\n')
        sys.exit (1)

    try:
        ifindex = params['ifindex']
    except:
        sys.stdout.write ('fence_ifmib: Error reading ifindex\n')
        sys.exit (1)

    try:
        option = params['option']
    except:
        option = 'off'

    return (comm, ipaddr, ifindex, option)
            

def snmpget (host, comm, oid):
    req = v2c.GETREQUEST ()
    encoded_oids = map (asn1.OBJECTID().encode, [oid,])
    req['community'] = comm
    tr = role.manager ((host, 161))
    rsp = v2c.RESPONSE ()
    (rawrsp, src) = tr.send_and_receive (req.encode (encoded_oids=encoded_oids))
    rsp.decode (rawrsp)
    if rsp['error_status']:
        raise IOError('SNMP error while reading')
    oids = map (lambda x: x[0], map(asn1.OBJECTID ().decode, rsp['encoded_oids']))
    vals = map (lambda x: x[0] (), map(asn1.decode, rsp['encoded_vals']))
    return vals[0]


def snmpset (host, comm, oid, type, value):
    req = v2c.SETREQUEST (request_id=random.randint (1,2**16-1))
    req['community'] = comm
    tr = role.manager ((host, 161))
    rsp = v2c.RESPONSE ()
    encoded_oids = map (asn1.OBJECTID ().encode, [oid,])
    encoded_vals = []
    encoded_vals.append (eval ('asn1.' + type + '()').encode (value))
    (rawrsp, src) = tr.send_and_receive (req.encode (encoded_oids=encoded_oids, encoded_vals=encoded_vals))
    rsp.decode(rawrsp)
    if rsp['error_status']:
        raise IOError('SNMP error while setting')
    oids = map (lambda x: x[0], map (asn1.OBJECTID().decode, rsp['encoded_oids']))
    vals = map (lambda x: x[0] (), map (asn1.decode, rsp['encoded_vals']))
    if vals[0] == value:
        return vals[0]
    else:
        raise IOError('SNMP error while setting')


def main():
    if len (sys.argv) > 1:
        (comm, host, index, option, verbose) = parseargs ()
    else:
        verbose = False
        (comm, host, index, option) = parsestdin ()

    try:
        switch = socket.gethostbyname (host)
    except socket.gaierror, err:
        vprint (verbose, 'fence_ifmib: %s' % str (err[1]))
        sys.exit(1)

    if option == 'on':
        value = up
    elif option == 'off':
        value = down
    elif option == 'status':
        value = None

    if value:
        # For option in (on, off) - write and verify
        try:
            r = snmpset (switch, comm, ifAdminStatus + str (index), 'INTEGER', value)
        except:
            sys.stderr.write ('fence_ifmib: Error during snmp write\n')
            sys.exit (1)
        
        try:
            s = int (snmpget (switch, comm, ifAdminStatus + str (index)))
        except:
            sys.stderr.write ('fence_ifmib: Error during fence verification\n')
            sys.exit (1)

        if s == value:
            vprint (verbose, 'fence_ifmib: action %s sucessful' % option)
            sys.exit (0)
        else:
            vprint (verbose, 'fence_ifmib: action %s failed' % option)
            sys.exit (1)
    else: # status
        try: 
            r = int (snmpget (switch, comm, ifAdminStatus + str (index)))
        except:
            sys.stderr.write ('fence_ifmib: Error during snmp read\n')
            sys.exit (1)

        if r == up:
            vprint (verbose, 'fence_ifmib: Port is admin up')
            sys.exit (0)
        elif r == down:
            vprint (verbose, 'fence_ifmib: Port is admin down')
            sys.exit (2)
        elif r == testing:
            vprint (verbose, 'fence_ifmib: Port is admin testing')
            sys.exit (2)


if __name__ == '__main__':
    main()

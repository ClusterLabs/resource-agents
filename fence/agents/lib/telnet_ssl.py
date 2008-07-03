#!/usr/bin/python

#####
## simple telnet client with SSL support 
##
## ./telnet_ssl host port
#####

import sys, socket, string, fcntl, os , time
from OpenSSL import SSL

#BEGIN_VERSION_GENERATION
RELEASE_VERSION=""
REDHAT_COPYRIGHT=""
BUILD_DATE=""
#END_VERSION_GENERATION

def main():
	hostname = None
	port = None

	if (len(sys.argv) != 3):
		print "Error: You have to enter hostname and port number\n"
		sys.exit(-1)

	hostname = sys.argv[1]
	port = int(sys.argv[2])

	try:
		s = socket.socket (socket.AF_INET, socket.SOCK_STREAM)
		s.connect((hostname,port))
		ctx = SSL.Context(SSL.SSLv23_METHOD)
		conn = SSL.Connection(ctx, s)
		conn.set_connect_state()
	except socket.error, e:
		print "Error: Unable to connect to %s:%s %s" % (hostname, port, str(e))
		sys.exit(-1)

	fcntl.fcntl(sys.stdin, fcntl.F_SETFL, os.O_NONBLOCK) 
	s.settimeout(0)

	while 1:
		try:
			write_buff = sys.stdin.readline()
			if (len(write_buff) > 0):
				write_buff = string.rstrip(write_buff)
				i = 10
				while i > 0:
					i = i-1
					try:
						conn.send(write_buff + "\r\n")
						i = -1
					except SSL.WantReadError:
						## We have to wait for connect, mostly just for first time
						time.sleep(1)
				if i == 0:
					sys.exit(-2)
		except IOError:
			1

		try:
			read_buff = conn.recv(4096)
			print read_buff
			sys.stdout.flush()
		except SSL.WantReadError:
			1
		except SSL.ZeroReturnError:
			break


if __name__ == "__main__":
	main()

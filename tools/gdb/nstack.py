import gdb
import gdb.printing
import string
import socket, struct

class arpEntryPrinter:
    "Print ARP table entry"

    def __init__(self, val):
        self.val = val

    def to_string(self):
        if self.val.address == 0:
            return 'NULL'

        ip_addr = socket.inet_ntoa(struct.pack('!L', int(self.val['ip_addr'])))
        haddr = self.val['haddr']
        age = self.val['age']
        if int(age) < 0:
            age = self.val['age'].cast(gdb.lookup_type('enum arp_cache_entry_type'))

        s = ip_addr + ' at ' + str(haddr) + ', age: ' + str(age)
        return s

def build_pretty_printer():
    pp = gdb.printing.RegexpCollectionPrettyPrinter("nstatck_arp")
    pp.add_printer('arp', '^arp_cache_entry$', arpEntryPrinter)

    return pp

gdb.printing.register_pretty_printer(
        gdb.current_objfile(),
        build_pretty_printer())

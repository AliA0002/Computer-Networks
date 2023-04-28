#!/usr/bin/env python
"""

Hub and spoke network, for EECS489, Winter 2023, Assignment 2
"""

from mininet.cli import CLI
from mininet.net import Mininet
from mininet.link import TCLink
from mininet.topo import Topo
from mininet.log import setLogLevel

class AssignmentNetworks(Topo):
    def __init__(self, **opts):
        Topo.__init__(self, **opts)
        h1 = self.addHost('h1')
        h2 = self.addHost('h2')
        h3 = self.addHost('h3')
        h4 = self.addHost('h4')
        h5 = self.addHost('h5')
        h6 = self.addHost('h6')
        h7 = self.addHost('h7')
        h8 = self.addHost('h8')
        h9 = self.addHost('h9')
        h10 = self.addHost('h10')
        s1 = self.addSwitch('s1')
        self.addLink(h1,  s1, bw=512, delay='5ms') # apace
        self.addLink(h2,  s1, bw=256, delay='5ms') # proxy
        self.addLink(h3,  s1, bw=128, delay='5ms') # b1 
        self.addLink(h4,  s1, bw=64,  delay='5ms')
        self.addLink(h5,  s1, bw=32,  delay='5ms') # b2
        self.addLink(h6,  s1, bw=16,  delay='5ms')
        self.addLink(h7,  s1, bw=8,   delay='5ms')
        self.addLink(h8,  s1, bw=4,   delay='5ms')
        self.addLink(h9,  s1, bw=2,   delay='5ms') 
        self.addLink(h10, s1, bw=1,   delay='5ms') 

        
if __name__ == '__main__':
    # setLogLevel( 'info' )
    setLogLevel( 'output' )

    # Create data network
    topo = AssignmentNetworks()
    net = Mininet(topo=topo, link=TCLink, autoSetMacs=True,
           autoStaticArp=True)
        #    autoStaticArp=True, xterms=1)

    # Run network
    net.start()
    CLI( net )
    net.stop()

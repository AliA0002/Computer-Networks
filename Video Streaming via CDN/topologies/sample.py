#!/usr/bin/env python
"""

Sample topolgy for geographic distance load balancer for EECS489, Winter 2023, Assignment 2
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
        s1 = self.addSwitch('s1')
        s2 = self.addSwitch('s2')
        self.addLink(h1, s1, bw=10, delay='10ms')
        self.addLink(h2, s1, bw=20, delay='10ms')
        self.addLink(h3, s2, bw=50, delay='60ms')
        self.addLink(h4, s2, bw=30, delay='10ms')
        self.addLink(s1, s2, bw=40, delay='10ms')
        
        
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

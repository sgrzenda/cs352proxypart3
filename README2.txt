VLAN Part 2

Group Members: Steven Grzenda and Craig Perkins


Contributions

Steve and Craig contributed to this project equally and worked together for the majority of it. Craig implemented the hash table portion and Steve handled the list and adding to the list. We built off of our old cold from part 1.


Architecture

The proxy starts by reading in a configuration file. This file contains the listening port for server mode, the peers to connect to in client mode and timeout periods. The proxy begins to listen at its port for connections and spawns threads for each connection. The proxy then attempts to send out connection requests to other servers listed in its config file. 

When a client connects to a server it will send it a link state packet. The server will then read it, add its member ship list to it and send it back to the client. The client will then attempt to add to these other servers if it has not already. 

Every predefined period the proxy will flood link state packets to all other proxies it is connected to. The receivers of these packets will attempt to connect to any members they are not currently connected to and add them to their lists.

Every predefined period the proxy will check to see if they haven’t heard from another proxy in a while. If they have timed out they will delete them from their lists and close the connection to that proxy.

Some proxies will have a predefined quit after period which will send a quit packet to all proxies it is connected to. This packet tells the other proxies to shut down.

The proxy can also deliver data packets from host tap to host tcp to client tcp to client tap. For example PING

Difficulties/Challenges

This project was extremely challenging. Many of the directions seemed to be unclear and some parts of the implementation explained part 2 and 3 at the same time, which caused confusion. The leave packets aren’t really explained and the exact format of the link state packets isn’t clear. This brings us to the next problem.

We needed at least three machines to test multiple connections. After being told a few times we would get a machine we never received it. However we were given a shared reference running someone else’s code. This code didn’t serialize the packets like ours did (because the specific packet format that the TA’s proxy was using wasn’t specified) so we could not use it. We each tried to contact the professor and received no response as of 11/26/13 at 3:00 PM. So therefore we could not adequately test our code with multiple connections. We have left in all of our implementation that should support multiple connections but as all computer scientists know that doesn’t mean it works.

There are also a few warnings upon compiling concerning signedness in the hash functions. Unfortunately this was not able to be corrected and still function so the warnings will remain. 

We left in our print statements in the proxies to prove 1) That the proxies are connecting, 2) that the packets are sent every X seconds, 3) that packets are time checked every Y seconds, 4) that the sockets and taps are reading N bytes 

*********************Therefore to test our code, please run our proxy code on all machines. IT WILL NOT WORK WITH THE REFERENCE CODE**********************

********This code also works with the assumption that in the config file, each peer starts the line with “peer”
eg.
peer hostname1 port1
peer hostname2 port2


*********Code also requires that a server is started first who has no peers to connect to and then others can be started who have peers 

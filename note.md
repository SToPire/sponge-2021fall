### Lab0

#### webget

首先实现的webget是一个对socket api的简单应用，依靠封装好的`TCPSocket()`类和`send()`系统调用即可完成。

#### Byte Stream

Lab0需要实现一个字节流的抽象。字节流可以看作是有容量限制的双端队列：读者和写者分别从两端读出和写入数据，但是任何时候字节流中暂存的字节数目不能超过给定的上限。

实现这个抽象看起来需要一个`std::list<char>`的数据结构，可以在字节流推进的时候避免移动字符。但是这样做需要把输入字符串拆成字符放进链表，可能得不偿失，此外实现起来也更复杂。其实`std::string`就足以完成这个任务。

---

### Lab1

#### 实验要求

Lab1实现一个字节流重组器，其输入是字节流的碎片（字符串），输出是有序的字节流。这些碎片可能相互重叠。

重组器接受到乱序的字节后，需要在自己的辅助数据结构中暂存它们，直到前面缺失的字节抵达，才能把它们放进字节流。并不是所有的乱序字节都应该被存储的，因为重组器也有容量限制，其大小等于底层字节流和辅助存储的大小之和，如实验手册中的图所示。

例如：假设重组器容量为10，字节流中存储着3个字节，且第一个未被读取的字节下标是2。则字节[2,4]位于字节流中，字节[5,11]可以被存放在重组器的辅助存储中。第12个及以后的字节应该被丢弃，因为这落在了重组器的滑动窗口之外。

#### 实现

需要维护字节流写入端的下标，以判断新字符串是否按序。

我使用一个链表作为辅助存储，链表元素是字符串和其头尾下标，这些字符串不会重叠并有序存放。这样做可以避免把字符串拆成字符进行存储，也不需要在字节流推进时把辅助存储中的元素也向后移动。

对于每个传入的字符串，需要先掐头去尾，截取其可以存进重组器的部分。之后遍历链表中每个元素，判断其与新字符串的位置关系，并进行可能的合并。完成处理后，如果字符串刚好按序，则放入字节流；否则存入链表。


---

### Lab2

#### WrappingInt

Lab2首先需要实现一个工具类，将TCP序列号（32位，可能wrap around）与64位整型数互相转换。实现时不需要记录序列号wrap around的次数，因为我们不需要根据这个次数从一个序列号唯一确定u64整型数，而是根据接口中提供的`checkpoint`参数选择最接近的。

这个接口似乎有些迷惑，因为可能有两个u64整型数和`checkpoint`一样接近，比如$n=0,isn=0,checkpoint=2^{16}$时，返回$0$或者$2^{32}$都满足要求。

#### TcpReceiver

在`TCPReceiver`类中需要实现3个函数。注意SYN和FIN位各自需要占据一个序列号即可。按照讲义中（第5页）的提示，`unwrap`时使用last reassembled byte作为checkpoint（约等于ackno，它们只相差1）。

疑惑：讲义中提示用`TCPSegment::length_in_sequence_space()`（可能是维护ackno？），但看起来维护ackno最方便的方法是直接用下层`StreamReassembler`的当前指针，注意考虑SYN和FIN的额外偏移即可。

---

### Lab3

本实验实现TCP的sender部分。`TCPSender`需要提供4个接口：

| 接口                          | 功能                                                         |
| ----------------------------- | ------------------------------------------------------------ |
| `fill_window()`               | 从输入字节流中尽可能多的读取数据并发送，直至填满窗口或字节流为空 |
| `ack_received(ackno, window)` | 更新ackno和窗口大小                                          |
| `tick(time)`                  | 更新当前时间                                                 |
| `send_empty_segment()`        | 发送一个长度为0的数据段                                      |

手册里已经很清楚地描述了这些接口具体的行为，因此这个实验大部分是面向用例编程。尽管如此，用例覆盖的也不甚全面，很可能当前的一些实现并不符合设计目的。

一些注意点：

1. 实验手册规定的行为并不和RFC6298完全符合。在这个实验中，重传计时器的RTO的初值是给定的（而非动态测定）。RTO指数后退的逻辑存在，但是实验手册要求在窗口大小为0时不进行指数后退，这个行为暂时没有在RFC标准中找到依据。
2. 为了实现简单，本实验不要求待确认的数据段被部分确认，即只当$ackno >= segno + seg.length$才认为整个数据段被确认接收。不存在部分确认会导致`bytes_in_flight()` 超过窗口大小，需要考虑特判这种情况（或许后续实验还要修改）。
3. `MAX_PAYLOAD_SIZE`只是限制了载荷的大小，并不是序列号空间的大小，不会影响SYN和FIN位。
4. 发送空数据段的方式：正常设置序列号（即设为ackno），不设置SYN和FIN位，载荷为空。
5. ackno必须满足$SND.UNA < SEG.ACK <= SND.NXT$，其中UNA是oldest unacknowledged sequence number，NXT是next sequence number to be sent。不满足条件的ackno应该被直接丢弃，不能影响其他状态。

---

### Lab4

#### 实验要求

本实验实现完整的`TCPConnection`，TCP通信的每一端都由Lab2/3中实现的`TCPReceiver`和`TCPSender`组成。按照实验要求，`TCPConnection`需要提供以下接口：

| 接口                    | 功能                            |
| ----------------------- | ------------------------------- |
| `connect()`             | 发起一条TCP链接                 |
| `write(data)`           | 通过sender发送数据              |
| `end_input_stream()`    | 结束数据发送（仍然可以接收）    |
| `segment_received(seg)` | 通过receiver接收一个数据段      |
| `active()`              | 判断TCP连接是否仍然处于活跃状态 |

#### 实现

Lab4的实现主要就是调用前面实验中实现的接口：例如`segment_received()`接分别调用receiver的`segment_received()`和sender的`ack_received()`方法，询问sender是否有数据需要发送，如果没有则生成空数据段准备发送。向待发送的数据段中填入receiver一侧的ackno和win字段，然后发送。

一些注意点：

1. 我一开始以为本部分需要根据TCP状态机设计所有的状态转移，事实上没有必要这么做。只要各部分接口的逻辑正确，TCP实现就会正确表现出状态机的行为。只有一些特例需要判断：如`LISTEN`下，TCP连接应该丢弃所有不含有SYN的数据段。

2. TCP连接在TIME-WAIT下需要继续存活一段时间，体现为实现中的`_linger_after_streams_finish`变量。这个变量初始值为true，只在TCP连接确认不需要进入TIME-WAIT状态的时候被置为false。

3. TCP连接的状态可以用四元组`(_sender.state, _receiver.state, active(), _linger_after_streams_finish)`表示，其中`active()==false && _linger_after_streams_finish==true`这种情况是不合法的，不应存在，这种情况体现在框架代码`TCPState()`的构造函数中。

   ```c
   TCPState::TCPState(const TCPSender &sender, const TCPReceiver &receiver, const bool active, const bool linger)
       : _sender(state_summary(sender))
       , _receiver(state_summary(receiver))
       , _active(active)
       , _linger_after_streams_finish(active ? linger : false) {}
   ```

4. 不能假设`end_input_stream()`一定会在输出流开启时被调用，从而断言会发送含有FIN的数据段。上层应用可能会对已经被关闭的输出流调用`end_input_stream()`。
5. 本实验除了和之前类似的模拟测试用例以外，还引入了真实应用场景进行测试。框架提供的`txrx.sh`创建了一个tun虚拟网卡设备，为我们的TCP实现提供了socket wrapper，然后让我们的实现分别作为通信的两端互相发送消息。

#### 心得

本实验使我对TCP协议有了更深的理解：

1. 我们熟悉的四次挥手只是主动关闭方所经历的`FIN_WAIT_1`$\rightarrow$`FIN_WAIT_2`$\rightarrow$`TIME_WAIT`这条状态转移路径。事实上，主动关闭方还存在其他的状态转移方式：

   1. 发送FIN之后，对端在发送ACK之前就发来了对端的FIN，这种情况可以理解为通信双方近乎同时想要终止连接。在这种情况下，通信双方都会从`FIN_WAIT_1`进入`CLOSING`状态，收到对方的ACK之后即可进入`TIME_WAIT`状态。
   2. 发送FIN之后，对端把ACK和对端的FIN合在一个数据段中发送。这种情况是四次挥手中的第二步和第三步被合并进行了，此时主动关闭方可以直接从`FIN_WAIT_1`进入`TIME_WAIT`状态。

2. `TIME_WAIT`的存在意义：通信的一方在关闭连接前，需要确认对方已经把要发送的数据全发出去了，并且已经完全收到了自己对这段数据的ack。为了确认这件事情，需要满足两者其一：

   1. 该通信方是被动关闭连接的一方，即在自己发送FIN之前，对方主动发来了FIN。此时由于己方数据还没发完，己方的ACK会和数据一起被发出去。这个长度不为0的数据段受到重传机制的保护，从而可以确保对方一定收到。这种情况下，己方不需要进入`TIME_WAIT`状态。
   2. 该通信方已经等待了足够久，并且确信对方没有重传任何数据段过来，即`TIME_WAIT`状态。如果对方没有收到自己对FIN的ACK，一定会重传FIN。如果没有观测到这样的重传，可以确信对方已经收到了ACK。

   一言蔽之，`TIME_WAIT`就是为了确保对方收到了自己的ACK，这个ACK确认己方收到了对方发来的FIN。

3. TCP Keep-Alives机制。根据RFC 9293的描述，TCP实现可以发送一个探活数据段判断一个idle的连接是否仍然存活。这个探活数据段的seq是不合法的，并且恰好满足$SEG.SEQ = SND.NXT-1$。收到探活数据段的一方需要发送ACK加以回应。

---

### Lab5

本实验实现了`NetworkInterface`中的ARP协议，作为沟通链路层和网络层的桥梁，主要涉及3个接口。

| 接口                                | 功能                             |
| ----------------------------------- | -------------------------------- |
| `send_datagram(datagram, next_hop)` | 将IP数据报发往下一跳指定的IP地址 |
| `recv_frame(frame)`                 | 接收到一份链路层帧               |
| `tick(time)`                        | 更新当前时间                     |

ARP的实现总体来说比较简单：

1. `send_datagram()`查询转发表，如果IP地址对应的mac地址已知，则直接发送数据报。否则广播一个ARP request，并将待发送的报文暂存起来。
2. `recv_frame()`查看收到的frame中的负载类型。如果是IP数据报则将其返回，如果是ARP报文，则根据其类型做出对应的处理。
3. `tick()`更新时间，同时删除过时（30秒）的转发表条目。

注意点：

1. 为了防止ARP请求在网络中洪泛，如果已经发出了对某个IP的ARP请求，后续（5秒内）对相同IP发送的数据报都应该被暂存起来，等查到了mac地址再发送。
2. 在ARP协议中，某设备只要接收到一个ARP报文，不管报文target中指定的目标是不是自己（如报文是被广播的），都应该在自己的转发表中记录该报文来源设备的(IP, MAC)映射关系。

---

### Lab6

本实验实现`Router`中的最长前缀路由匹配算法。`Router`由若干个`NetworkInterface`和若干条路由规则组成，对于一次路由请求，`Router`将IP数据报的ttl值减1，找到最匹配的路由规则。如果规则中指定了下一跳路由器，就将数据报发往下一个路由目标；否则说明数据报的目的地就在当前路由器某个`NetworkInterface`的子网中，直接将数据报发往dst指定的IP地址即可。
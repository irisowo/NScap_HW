#include <stdio.h>
#include <pcap.h>
#include <stdlib.h> // for exit()
#include <string.h> //for memset
#include <string>
#include <set>
#include <sys/socket.h>
#include <arpa/inet.h> // for inet_ntoa()
#include <net/ethernet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>	//Provides declarations for icmp header
#include <netinet/udp.h>	//Provides declarations for udp header
#include <netinet/tcp.h>	//Provides declarations for tcp header
#include <netinet/ip.h>	//Provides declarations for ip heade
#include <iostream>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

//===================================Global====================================

char filter_exp[] = "";	// The filter expression
struct bpf_program fp;	// The compiled filter expression
pcap_t *handle;			// Session handle
std::set<std::string> set_ip;
int host_id = 3;
int re_compile = 0;
std::string new_filter;

//=============================================================================

typedef struct eth_hdr
{
    u_char dst_mac[6];
    u_char src_mac[6];
    u_short eth_type;
}eth_hdr;
eth_hdr *ethernet;
eth_hdr *inner_ethernet;

typedef struct gre_hdr
{
    u_char CRKSsRecur:8;
    u_char flags:5;
    u_char version:3;
    u_short protocol;
}gre_hdr;
gre_hdr *gre;


typedef struct ip_hdr
{
    int version:4;
    int header_len:4;
    u_char tos:8;//type of service
    int total_len:16;
    int ident:16;
    int flags:16;//3+13
    u_char ttl:8;
    u_char protocol:8;
    int checksum:16;
    u_char sourceIP[4];
    u_char destIP[4];
}ip_hdr;
ip_hdr *ip;
ip_hdr *inner_ip;

//==============================================================================

void establish_tunnel(std::string remote_ip){

    // Convert string to const char * as system requires
    std::string gretap_name = "GRE_h";
    if(remote_ip[6]=='4') {gretap_name += "1";}
    else if(remote_ip[6]=='5') {gretap_name += "2";}
    else {gretap_name += std::to_string(host_id++);}//start from 3

    // ip link set gretap and add to bridge
    std::string cmd1 = "ip link add " + gretap_name + " type gretap remote " + remote_ip + " local 140.113.0.1";
    std::string cmd2 = "ip link set " + gretap_name + " up";
    std::string cmd3 = "brctl addif br0 " + gretap_name;
    std::system(cmd1.c_str());
    std::system(cmd2.c_str());
    std::system(cmd3.c_str());
    std::system("ip link set br0 up");
}

//==============================================================================

void pcap_callback(unsigned char * arg,const struct pcap_pkthdr *packet_header,const unsigned char *packet_content){
    static int id=1;
    printf("Packet Num [%d]\n",id++);
    pcap_dump(arg,packet_header,packet_content);
    printf("Packet length : %d\n",packet_header->len);
    //printf("Number of bytes : %d\n",packet_header->caplen);
    printf("Content : \n");
    int i;
    for(i=0;i<packet_header->caplen;i++){
        printf(" %02x",packet_content[i]);
        if((i+1)%16==0){
            printf("\n");
        }
    }
    printf("\n");

    u_int eth_len=sizeof(struct eth_hdr);
    u_int ip_len=sizeof(struct ip_hdr);
    u_int gre_len = sizeof(struct gre_hdr);
    u_int inner_eth_len=sizeof(struct eth_hdr);
    u_int inner_ip_len=sizeof(struct ip_hdr);

    ethernet=(eth_hdr *)packet_content;
    printf("Source Mac: %02x-%02x-%02x-%02x-%02x-%02x\n",ethernet->src_mac[0],ethernet->src_mac[1],ethernet->src_mac[2],ethernet->src_mac[3],ethernet->src_mac[4],ethernet->src_mac[5]);
    printf("Destination Mac : %02x-%02x-%02x-%02x-%02x-%02x\n",ethernet->dst_mac[0],ethernet->dst_mac[1],ethernet->dst_mac[2],ethernet->dst_mac[3],ethernet->dst_mac[4],ethernet->dst_mac[5]);

    //--------------------print Outer info-----------------------
    if(ntohs(ethernet->eth_type)==0x0800){
        printf("Outer Ethernet type: 0x%04x (IPv4)\n",ntohs(ethernet->eth_type));
        ip=(ip_hdr*)(packet_content+eth_len);
        printf("Outer Src IP : %d.%d.%d.%d\n",ip->sourceIP[0],ip->sourceIP[1],ip->sourceIP[2],ip->sourceIP[3]);
        printf("Outer Dst ip : %d.%d.%d.%d\n",ip->destIP[0],ip->destIP[1],ip->destIP[2],ip->destIP[3]);
        //--------------------update filter----------------------
        std::string src_ip_str = "";
        for(int i=0;i<4;i++){
            src_ip_str += i ? "."+std::string(std::to_string((int)ip->sourceIP[i])) : std::string(std::to_string((int)ip->sourceIP[i]));
        }
        std::pair<std::set<std::string>::iterator,bool> ret;
        ret = set_ip.insert(src_ip_str);
        if (ret.second==false){
            std::string array2 = " and not ip src host " + src_ip_str;
            new_filter = new_filter + array2;
            if (pcap_compile(handle, &fp, new_filter.c_str(), 0,
PCAP_NETMASK_UNKNOWN) == -1) {
                fprintf(stderr, "Couldn't parse filter %s: %s\n", filter_exp, pcap_geterr(handle));
            }
            if(pcap_setfilter(handle,&fp)<0){
                printf("error\n");
            }
            printf("              UPDATE               \n");
            std::cout<<"FILTER : "<<new_filter << std::endl;
            if(src_ip_str[6] != '3') {
                establish_tunnel(src_ip_str);
            }
        }

        //--------------------check GRE or not----------------------

        gre=(gre_hdr *)(packet_content+eth_len+ip_len);
        if(ntohs(gre->flags)==0x0 && ntohs(gre->version)==0x0){
            printf("Next Layer Protocol: GRE\n");
       }

        //--------------------print Inner info-----------------------
        inner_ethernet=(eth_hdr *)(packet_content+eth_len+ip_len+gre_len);
        printf("Inner Source Mac: %02x-%02x-%02x-%02x-%02x-%02x\n",inner_ethernet->src_mac[0],inner_ethernet->src_mac[1],inner_ethernet->src_mac[2],inner_ethernet->src_mac[3],inner_ethernet->src_mac[4],inner_ethernet->src_mac[5]);
    printf("Inner Destination Mac : %02x-%02x-%02x-%02x-%02x-%02x\n",inner_ethernet->dst_mac[0],inner_ethernet->dst_mac[1],inner_ethernet->dst_mac[2],inner_ethernet->dst_mac[3],inner_ethernet->dst_mac[4],inner_ethernet->dst_mac[5]);
        if(ntohs(inner_ethernet->eth_type)==0x0800){
            printf("ethernet type : 0x%04x\n (ipv4)",ntohs(inner_ethernet->eth_type));
            inner_ip=(ip_hdr*)(packet_content+eth_len+ip_len+4+inner_eth_len);
            printf("Inner Src IP : %d.%d.%d.%d\n",inner_ip->sourceIP[0],inner_ip->sourceIP[1],inner_ip->sourceIP[2],inner_ip->sourceIP[3]);
            printf("Inner Dst ip : %d.%d.%d.%d\n",inner_ip->destIP[0],inner_ip->destIP[1],inner_ip->destIP[2],inner_ip->destIP[3]);
        }
        else {
            printf("ethernet type : 0x%04x\n",ntohs(inner_ethernet->eth_type));
        }
    }
    else {
        printf("ipv6 is used, ethernet type : 0x%04x\n",ntohs(ethernet->eth_type));
        //printf("ipv6 is used\n");
    }
    printf("--------------------------------------------\n");
}

//==============================================================================
int main(int argc, char *argv[])
{
    char errorbuf[PCAP_ERRBUF_SIZE];
    char devs[20][10]; // Store the name of interfaces
    pcap_if_t *interfaces, *temp;
    int i = 0;

    std::system("ip link add br0 type bridge");
    std::system("brctl addif br0 BRGr-eth0");
    std::system("ip link set br0 up");

    //====================select the interface====================
    if(pcap_findalldevs(&interfaces,errorbuf) == -1){
        printf("error in pcap findall devs");
    }

    for(temp = interfaces;temp;temp = temp->next){
        printf("%d Name:  %s\n",i,temp->name);
		if(temp->name != NULL){
			strcpy( devs[i++] , temp->name);
		}
    }
    std::cout<<("Insert a number to sekect interface\n");
    int interface_id = 0;
    std::cin  >>  interface_id ;
    char *dev = devs[interface_id];

    //====================open the interface====================
    std::cout<<"Start listening at $"<< dev << std::endl;
    handle = pcap_open_live(dev, BUFSIZ, 1, 1000, errorbuf);
    if (handle == NULL) {
    	fprintf(stderr, "Couldn't open device %s: %s\n", dev, errorbuf);
	return(2);
    }

    //====================compile filter====================
    //filter_exp = "";	// The filter expression
    std::cout << "Insert BPF filter expression:" << std::endl;
    std::cin.ignore();
    std::cin.getline (filter_exp, 100);
    new_filter = (filter_exp);

    if (pcap_compile(handle, &fp, filter_exp, 0, PCAP_NETMASK_UNKNOWN) == -1) {
        fprintf(stderr, "Couldn't parse filter %s: %s\n", filter_exp, pcap_geterr(handle));
	return(2);
    }
    if(pcap_setfilter(handle,&fp)<0){
        printf("error\n");
        return 0;
    }

    //========================packet========================
    printf("------------------Packet-------------------\n");
    int id=0;
    pcap_dumper_t* dumpfp=pcap_dump_open(handle,"./save1.pcap");
    if(pcap_loop(handle,15,pcap_callback,(unsigned char *)dumpfp)<0){
        printf("error\n");
        return 0;
    }
    printf("\n");

    //========================close========================
    pcap_dump_close(dumpfp);
    pcap_close(handle);
    return 0;

}


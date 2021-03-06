/**
 * @file   ebbchar.c
 * @author Derek Molloy_ Andy SH Lee _ Payam Naghdi_ Amirhossein Kargaran
 * @version 0.1
 * @brief   An introductory character driver to support the second article of my series on
 * Linux loadable kernel module (LKM) development. This module maps to /dev/ebbchar and
 * comes with a helper C program that can be run in Linux user space to communicate with
 * this the LKM.
 * @see http://www.derekmolloy.ie/ , https://github.com/a110605/packet_mangling and https://github.com/payamnaghdy/ICMPdropko/blob/master/packet.c for a full description and follow-up descriptions.
 */
#include <linux/kernel.h>         // Contains types, macros, functions for the kernel
#include <linux/module.h>         // Core header for loading LKMs into the kernel
#include <linux/netfilter.h>      //Filter Header
#include <linux/netfilter_ipv4.h> //Filter Header For IPV4
#include <linux/skbuff.h>
#include <linux/udp.h>
#include <linux/icmp.h>
#include <linux/ip.h>
#include <linux/inet.h>
#include <linux/init.h>           // Macros used to mark up functions e.g. __init __exit
#include <linux/device.h>         // Header to support the kernel Driver Model
#include <linux/fs.h>             // Header for the Linux file system support
#include <linux/uaccess.h>          // Required for the copy to user function

#define  DEVICE_NAME "ebbchar"    ///< The device will appear at /dev/ebbchar using this value
#define  CLASS_NAME  "ebb"        ///< The device class -- this is a character device driver

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Amirhossein Kargaran");
MODULE_DESCRIPTION("Filter packet module");
MODULE_VERSION("0.1");

struct sk_buff *sock_buff;
struct iphdr *ip_header;
struct udphdr * udp_headers;
int port ;
unsigned int icmp_hook(unsigned int hooknum, struct sk_buff *skb,
                       const struct net_device *in, const struct net_device *out,
                       int(*okfn)(struct sk_buff *));
					   
static struct nf_hook_ops icmp_drop __read_mostly = {
        .pf = NFPROTO_IPV4,
        .priority = NF_IP_PRI_FIRST,
        .hooknum =NF_INET_LOCAL_IN,
        .hook = (nf_hookfn *) icmp_hook
};
static int mode=0;
static char address[24];
static int count=0;							///<number of packets
static int    majorNumber;                  ///< Stores the device number -- determined automatically
static char   message[24] = {0};           ///< Memory for the string that is passed from userspace
static short  size_of_message;              ///< Used to remember the size of the string stored
static int    numberOpens = 0;              ///< Counts the number of times the device is opened
static struct class*  ebbcharClass  = NULL; ///< The device-driver class struct pointer
static struct device* ebbcharDevice = NULL; ///< The device-driver device struct pointer
static char filtering[100][24];               ///<filtering table 


// The prototype functions for the character driver -- must come before the struct definition
static int     fuckdev_open(struct inode *, struct file *);
static int     fuckdev_release(struct inode *, struct file *);
static ssize_t fuckdev_read(struct file *, char *, size_t, loff_t *);
static ssize_t fuckdev_write(struct file *, const char *, size_t, loff_t *);

static struct file_operations fops =
{
   .open = fuckdev_open,
   .read = fuckdev_read,
   .write = fuckdev_write,
   .release = fuckdev_release,
};

static int __init ebbchar_init(void){
	printk(KERN_INFO "packet dropper loaded\n");
	int ret = nf_register_net_hook(&init_net,&icmp_drop); /*Record in net filtering */
	if(ret)
	{
		printk(KERN_INFO "FAILED");
		return  ret;
	}
   printk(KERN_INFO "EBBChar: Initializing the EBBChar LKM\n");

   // Try to dynamically allocate a major number for the device -- more difficult but worth it
   majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
   if (majorNumber<0){
      printk(KERN_ALERT "EBBChar failed to register a major number\n");
      nf_unregister_net_hook(&init_net,&icmp_drop); /*UnRecord in net filtering */
      return majorNumber;
   }
   printk(KERN_INFO "EBBChar: registered correctly with major number %d\n", majorNumber);

   // Register the device class
   ebbcharClass = class_create(THIS_MODULE, CLASS_NAME);
   if (IS_ERR(ebbcharClass)){                // Check for error and clean up if there is
      unregister_chrdev(majorNumber, DEVICE_NAME);
      printk(KERN_ALERT "Failed to register device class\n");
      nf_unregister_net_hook(&init_net,&icmp_drop); /*UnRecord in net filtering */
      return PTR_ERR(ebbcharClass);          // Correct way to return an error on a pointer
   }
   printk(KERN_INFO "EBBChar: device class registered correctly\n");

   // Register the device driver
   ebbcharDevice = device_create(ebbcharClass, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
   if (IS_ERR(ebbcharDevice)){               // Clean up if there is an error
      class_destroy(ebbcharClass);           // Repeated code but the alternative is goto statements
      unregister_chrdev(majorNumber, DEVICE_NAME);
      printk(KERN_ALERT "Failed to create the device\n");
      nf_unregister_net_hook(&init_net,&icmp_drop); /*UnRecord in net filtering */ //Payam icmp_drop_exit
      return PTR_ERR(ebbcharDevice);
   }
   printk(KERN_INFO "EBBChar: device class created correctly\n"); // Made it! device was initialized
   return 0;
}
/** @brief The LKM cleanup function
 *  Similar to the initialization function, it is static. The __exit macro notifies that if this
 *  code is used for a built-in driver (not a LKM) that this function is not required.
 */
static void __exit ebbchar_exit(void){
   device_destroy(ebbcharClass, MKDEV(majorNumber, 0));     // remove the device
   class_unregister(ebbcharClass);                          // unregister the device class
   class_destroy(ebbcharClass);                             // remove the device class
   unregister_chrdev(majorNumber, DEVICE_NAME);             // unregister the major number
   printk(KERN_INFO "EBBChar: Goodbye from the LKM!\n");
   nf_unregister_net_hook(&init_net,&icmp_drop); /*UnRecord in net filtering */ //Payam icmp_drop_exit
}
/** @brief The device open function that is called each time the device is opened
 *  This will only increment the numberOpens counter in this case.
 *  @param inodep A pointer to an inode object (defined in linux/fs.h)
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 */
static int fuckdev_open(struct inode *inodep, struct file *filep){
   numberOpens++;
   printk(KERN_INFO "EBBChar: Device has been opened %d time(s)\n", numberOpens);
   return 0;
}
/** @brief This function is called whenever device is being read from user space i.e. data is
 *  being sent from the device to the user. In this case is uses the copy_to_user() function to
 *  send the buffer string to the user and captures any errors.
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 *  @param buffer The pointer to the buffer to which this function writes the data
 *  @param len The length of the b
 *  @param offset The offset if required
 */
static ssize_t fuckdev_read(struct file *filep, char *buffer, size_t len, loff_t *offset){
   int error_count = 0;
   // copy_to_user has the format ( * to, *from, size) and returns 0 on success
   error_count = copy_to_user(buffer, message, size_of_message);

   if (error_count==0){            // if true then have success
      printk(KERN_INFO "EBBChar: Sent %d characters to the user\n", size_of_message);
      return (size_of_message=0);  // clear the position to the start and return 0
   }
   else {
      printk(KERN_INFO "EBBChar: Failed to send %d characters to the user\n", error_count);
      return -EFAULT;              // Failed -- return a bad address message (i.e. -14)
   }
}
/** @brief This function is called whenever the device is being written to from user space i.e.
 *  data is sent to the device from the user. The data is copied to the message[] array in this
 *  LKM using the sprintf() function along with the length of the string.
 *  @param filep A pointer to a file object
 *  @param buffer The buffer to that contains the string to write to the device
 *  @param len The length of the array of data that is being passed in the const char buffer
 *  @param offset The offset if required
 */
static ssize_t fuckdev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset){
   sprintf(message, "%s", buffer, len);   // appending received string with its length
   size_of_message = strlen(message);                 // store the length of the stored message
   message[size_of_message -1] = 0;
   printk(KERN_INFO "EBBChar: Received %zu characters from the user\n", len);
   if(!strcmp(message,"White"))
   {
	mode = 1;
	count = 0;
	printk(KERN_INFO "EBBChar: White Mode.\n");
   }
   else if(!strcmp(message,"Black"))
   {
	mode = 2;
	count = 0;
	printk(KERN_INFO "EBBChar: Black Mode.\n");
   }
   else if(count < 100)
   {
	sprintf(filtering[count],message);
	printk(KERN_INFO "EBBChar: Address %s saved \n",message);
	count++;
   }
   return len;
}

static int fuckdev_release(struct inode *inodep, struct file *filep){
   printk(KERN_INFO "EBBChar: Device successfully closed\n");
   return 0;
}
/*
unsigned int icmp_hook(unsigned int hooknum, struct sk_buff *skb,

        const struct net_device *in, const struct net_device *out,

        int(*okfn)(struct sk_buff *))

{
        sock_buff = skb;
        ip_header = (struct iphdr *)skb_network_header(sock_buff);
        if(!sock_buff) { return NF_DROP;}
         if (ip_header->protocol==IPPROTO_ICMP) {
             printk(KERN_INFO "Got ICMP Reply packet and dropped it. \n");
             return NF_DROP;
         }
         return NF_ACCEPT;

}
*/
unsigned int icmp_hook(unsigned int hooknum, struct sk_buff *skb,

        const struct net_device *in, const struct net_device *out,

        int(*okfn)(struct sk_buff *))

{
        sock_buff = skb;
        ip_header = (struct iphdr *)skb_network_header(sock_buff);
		udp_headers = (struct udphdr *) skb_transport_header(sock_buff);
		port = ((unsigned char *)&udp_headers->source)[0] * 256 + ((unsigned char *)&udp_headers->source)[1];
	sprintf(address,"%d.%d.%d.%d:%d",((unsigned char *)&(ip_header->saddr))[0],((unsigned char *)&(ip_header->saddr))[1],((unsigned char *)&(ip_header->saddr))[2],((unsigned char *)&(ip_header->saddr))[3],port);
	
	for(int i=0;i<count;i++)
	{
		if(mode==2)
		{
			if(!strcmp(address,filtering[i]))
			{
				printk(KERN_INFO "EBBChar: Packet from %s dropped\n", address);
				return NF_DROP;
			}
		}
		else if(mode==1)
		{
			if(!strcmp(address,filtering[i]))
			{
				printk(KERN_INFO "EBBChar: Packet from %s accepted\n", address);
				return NF_ACCEPT;
			}
		}
	}
}
module_init(ebbchar_init);
module_exit(ebbchar_exit);
/**********
This is taken from SIMH.  This program just lists the available ethernet devices in the system...

Network devices:
  Number       NAME                                     (Description)
  0  \Device\NPF_{E07F14CE-C54C-4D82-A94C-60A4C4BE23E7} (Local Area Connection 4)
  1  \Device\NPF_{759B3B30-C0B1-4E62-ADC9-DF54D42A0813} (Bluetooth Network Connection)
  2  \Device\NPF_{D7198B2D-5D20-4846-893C-B353ECE3D4E6} (Local Area Connection)
Press Enter to continue...

**********/

#define ETH_DEV_NAME_MAX     256                        /* maximum device name size */
#define ETH_DEV_DESC_MAX     256                        /* maximum device description size */
#define ETH_MAX_DEVICE        30                        /* maximum ethernet devices */
#define ETH_PROMISC            1                        /* promiscuous mode = true */
#define ETH_MAX_PACKET      1514                        /* maximum ethernet packet size */
#define PCAP_READ_TIMEOUT -1

struct eth_list {
  int     num;
  char    name[ETH_DEV_NAME_MAX];
  char    desc[ETH_DEV_DESC_MAX];
};

typedef struct eth_list ETH_LIST;

#include <pcap.h>
#include <string.h>

int eth_devices(int max, ETH_LIST* list);
int eth_host_devices(int used, int max, ETH_LIST* list);

//t_stat eth_show (FILE* st, UNIT* uptr, int32 val, void* desc)
void main()
{
  ETH_LIST  list[ETH_MAX_DEVICE];
  int number = eth_devices(ETH_MAX_DEVICE, list);

  printf("Network devices:\n");
  if (number == -1)
    printf("  network support not available in simulator\n");
  else
    if (number == 0)
      printf("  no network devices are available\n");
    else {
      int i, min, len;
	  
	  printf("  Number       NAME\t\t\t\t\t(Description)\n");
      for (i=0, min=0; i<number; i++)
        if ((len = strlen(list[i].name)) > min) min = len;
      for (i=0; i<number; i++)
        printf("  %d  %-*s (%s)\n", i, min, list[i].name, list[i].desc);
    }
	printf("Press Enter to continue...\n");
	getch();
  return;
}


int eth_devices(int max, ETH_LIST* list)
{
  pcap_if_t* alldevs;
  pcap_if_t* dev;
  int i = 0;
  char errbuf[PCAP_ERRBUF_SIZE];

#ifndef DONT_USE_PCAP_FINDALLDEVS
  /* retrieve the device list */
  if (pcap_findalldevs(&alldevs, errbuf) == -1) {
    char* msg = "Eth: error in pcap_findalldevs: %s\r\n";
    printf (msg, errbuf);
//    if (sim_log) fprintf (sim_log, msg, errbuf);
  } else {
    /* copy device list into the passed structure */
    for (i=0, dev=alldevs; dev; dev=dev->next) {
      if ((dev->flags & PCAP_IF_LOOPBACK) || (!strcmp("any", dev->name))) continue;
      list[i].num = i;
      sprintf(list[i].name, "%s", dev->name);
      if (dev->description)
        sprintf(list[i].desc, "%s", dev->description);
      else
        sprintf(list[i].desc, "%s", "No description available");
      if (i++ >= max) break;
    }

    /* free device list */
    pcap_freealldevs(alldevs);
  }
#endif

  /* Add any host specific devices and/or validate those already found */
  i = eth_host_devices(i, max, list);

  /* return device count */
  return i;
}

int eth_host_devices(int used, int max, ETH_LIST* list)
{
  pcap_t* conn;
  int i, j, datalink;
  char errbuf[PCAP_ERRBUF_SIZE];

  for (i=0; i<used; ++i) {
    /* Cull any non-ethernet interface types */
    conn = pcap_open_live(list[i].name, ETH_MAX_PACKET, ETH_PROMISC, PCAP_READ_TIMEOUT, errbuf);
    if (NULL != conn) datalink = pcap_datalink(conn), pcap_close(conn);
    if ((NULL == conn) || (datalink != DLT_EN10MB)) {
      for (j=i; j<used-1; ++j)
        list[j] = list[j+1];
      --used;
      --i;
    }
  } /* for */

#if defined(_WIN32)
  /* replace device description with user-defined adapter name (if defined) */
  for (i=0; i<used; i++) {
        char regkey[2048];
    char regval[2048];
        LONG status;
    DWORD reglen, regtype;
    HKEY reghnd;

        /* These registry keys don't seem to exist for all devices, so we simply ignore errors. */
        /* Windows XP x64 registry uses wide characters by default,
            so we force use of narrow characters by using the 'A'(ANSI) version of RegOpenKeyEx.
            This could cause some problems later, if this code is internationalized. Ideally,
            the pcap lookup will return wide characters, and we should use them to build a wide
            registry key, rather than hardcoding the string as we do here. */
        if(list[i].name[strlen( "\\Device\\NPF_" )] == '{') {
              sprintf( regkey, "SYSTEM\\CurrentControlSet\\Control\\Network\\"
                            "{4D36E972-E325-11CE-BFC1-08002BE10318}\\%hs\\Connection", list[i].name+
                            strlen( "\\Device\\NPF_" ) );
              if((status = RegOpenKeyExA (HKEY_LOCAL_MACHINE, regkey, 0, KEY_QUERY_VALUE, &reghnd)) != ERROR_SUCCESS) {
                  continue;
              }
        reglen = sizeof(regval);

      /* look for user-defined adapter name, bail if not found */    
        /* same comment about Windows XP x64 (above) using RegQueryValueEx */
      if((status = RegQueryValueExA (reghnd, "Name", NULL, &regtype, regval, &reglen)) != ERROR_SUCCESS) {
              RegCloseKey (reghnd);
            continue;
        }
      /* make sure value is the right type, bail if not acceptable */
        if((regtype != REG_SZ) || (reglen > sizeof(regval))) {
            RegCloseKey (reghnd);
            continue;
        }
      /* registry value seems OK, finish up and replace description */
        RegCloseKey (reghnd );
      sprintf (list[i].desc, "%s", regval);
    }
  } /* for */
#endif

    return used;
}

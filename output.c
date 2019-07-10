#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <time.h>
#include <netdb.h>
#include <errno.h>
#ifdef HAVE_LIBACARS
#include <libacars/libacars.h>
#include <libacars/acars.h>
#include <libacars/vstring.h>
#endif
#include "acarsdec.h"
#include "cJSON.h"
extern int label_filter(char *lbl);

extern int inmode;
extern char *idstation;

static int sockfd = -1;
static FILE *fdout;
static char *filename_prefix = NULL;
static char *extension = NULL;
static size_t prefix_len;
static struct tm current_tm;

static char *jsonbuf=NULL;
#define JSONBUFLEN 30000

static inline void cls(void)
{
	printf("\x1b[H\x1b[2J");
}

static int open_outfile() {
	char *filename = NULL;
	char *fmt = NULL;
	size_t tlen = 0;

	if(hourly || daily) {
		time_t t = time(NULL);
		gmtime_r(&t, &current_tm);
		char suffix[16];
		if(hourly) {
			fmt = "_%Y%m%d_%H";
		} else {	// daily
			fmt = "_%Y%m%d";
		}
		tlen = strftime(suffix, sizeof(suffix), fmt, &current_tm);
		if(tlen == 0) {
			fprintf(stderr, "open_outfile(): strfime returned 0\n");
			return -1;
		}
		filename = calloc(prefix_len + tlen + 2, sizeof(char));
		if(filename == NULL) {
			fprintf(stderr, "open_outfile(): failed to allocate memory\n");
			return -1;
		}
		sprintf(filename, "%s%s%s", filename_prefix, suffix, extension);
	} else {
		filename = strdup(filename_prefix);
	}

	if((fdout = fopen(filename, "a+")) == NULL) {
		fprintf(stderr, "Could not open output file %s: %s\n", filename, strerror(errno));
		free(filename);
		return -1;
	}
	free(filename);
	return 0;
}

int initOutput(char *logfilename, char *Rawaddr)
{
	char *addr;
	char *port;
	struct addrinfo hints, *servinfo, *p;
	int rv;

	if (outtype != OUTTYPE_NONE && logfilename) {
		filename_prefix = logfilename;
		prefix_len = strlen(filename_prefix);
		if(hourly || daily) {
			char *basename = strrchr(filename_prefix, '/');
			if(basename != NULL) {
				basename++;
			} else {
				basename = filename_prefix;
			}
			char *ext = strrchr(filename_prefix, '.');
			if(ext != NULL && (ext <= basename || ext[1] == '\0')) {
				ext = NULL;
			}
			if(ext) {
				extension = strdup(ext);
				*ext = '\0';
			} else {
				extension = strdup("");
			}
		}
		if(open_outfile() < 0)
			return -1;
	} else {
		fdout = stdout;
		hourly = daily = 0;	// stdout is not rotateable
	}

	if (Rawaddr) {

		memset(&hints, 0, sizeof hints);
		if (Rawaddr[0] == '[') {
			hints.ai_family = AF_INET6;
			addr = Rawaddr + 1;
			port = strstr(addr, "]");
			if (port == NULL) {
				fprintf(stderr, "Invalid IPV6 address\n");
				return -1;
			}
			*port = 0;
			port++;
			if (*port != ':')
				port = "5555";
			else
				port++;
		} else {
			hints.ai_family = AF_UNSPEC;
			addr = Rawaddr;
			port = strstr(addr, ":");
			if (port == NULL)
				port = "5555";
			else {
				*port = 0;
				port++;
			}
		}

		hints.ai_socktype = SOCK_DGRAM;

		if ((rv = getaddrinfo(addr, port, &hints, &servinfo)) != 0) {
			fprintf(stderr, "Invalid/unknown address %s\n", addr);
			return -1;
		}

		for (p = servinfo; p != NULL; p = p->ai_next) {
			if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			continue;
			}

			if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
				close(sockfd);
				continue;
			}
			break;
		}
		if (p == NULL) {
			fprintf(stderr, "failed to connect\n");
			return -1;
		}

		freeaddrinfo(servinfo);
	}

	if (outtype == OUTTYPE_MONITOR ) {
		verbose=0;
		cls();
		fflush(stdout);
	}

	if (outtype == OUTTYPE_JSON || outtype == OUTTYPE_ROUTEJSON || (sockfd && netout==NETLOG_JSON)) {
		jsonbuf = malloc(JSONBUFLEN+1);
	}
	
	return 0;
}

static int rotate_outfile() {
	struct tm new_tm;
	time_t t = time(NULL);
	gmtime_r(&t, &new_tm);
	if((hourly && new_tm.tm_hour != current_tm.tm_hour) ||
	   (daily && new_tm.tm_mday != current_tm.tm_mday)) {
		fclose(fdout);
		return open_outfile();
	}
	return 0;
}

static void printtime(struct timeval tv)
{
	struct tm tmp;

	gmtime_r(&(tv.tv_sec), &tmp);

	fprintf(fdout, "%02d:%02d:%02d",
		tmp.tm_hour, tmp.tm_min, tmp.tm_sec);
}

static void printdate(struct timeval tv)
{
	struct tm tmp;

	if (tv.tv_sec + tv.tv_usec == 0)
		return;

	gmtime_r(&(tv.tv_sec), &tmp);

	fprintf(fdout, "%02d/%02d/%04d ",
		tmp.tm_mday, tmp.tm_mon + 1, tmp.tm_year + 1900);
	printtime(tv);
}

void outpp(acarsmsg_t * msg)
{
	char pkt[500];
	char txt[250];
	char *pstr;

	strcpy(txt, msg->txt);
	for (pstr = txt; *pstr != 0; pstr++)
		if (*pstr == '\n' || *pstr == '\r')
			*pstr = ' ';

	sprintf(pkt, "AC%1c %7s %1c %2s %1c %4s %6s %s",
		msg->mode, msg->addr, msg->ack, msg->label, msg->bid, msg->no,
		msg->fid, txt);

	write(sockfd, pkt, strlen(pkt));
}

void outsv(acarsmsg_t * msg, int chn, struct timeval tv)
{
	char pkt[500];
	struct tm tmp;

	gmtime_r(&(tv.tv_sec), &tmp);

	sprintf(pkt,
		"%8s %1d %02d/%02d/%04d %02d:%02d:%02d %1d %03d %1c %7s %1c %2s %1c %4s %6s %s",
		idstation, chn + 1, tmp.tm_mday, tmp.tm_mon + 1,
		tmp.tm_year + 1900, tmp.tm_hour, tmp.tm_min, tmp.tm_sec,
		msg->err, msg->lvl, msg->mode, msg->addr, msg->ack, msg->label,
		msg->bid, msg->no, msg->fid, msg->txt);

	write(sockfd, pkt, strlen(pkt));
}

void outjson()
{
	char pkt[500];

	snprintf(pkt, sizeof(pkt), "%s\n", jsonbuf);
	write(sockfd, pkt, strlen(pkt));
}

#ifdef HAVE_LIBACARS
void decode_and_print_acars_apps(acarsmsg_t * msg) {
	if(msg->txt[0] == '\0')
		return;

	la_msg_dir direction;
	if(msg->bid >= '0' && msg->bid <= '9')
		direction = LA_MSG_DIR_AIR2GND;
	else
		direction = LA_MSG_DIR_GND2AIR;

	la_proto_node *node = la_acars_decode_apps(msg->label, msg->txt, direction);
	if(node != NULL) {
		la_vstring *vstr = la_proto_tree_format_text(NULL, node);
		fprintf(fdout, "%s\n", vstr->str);
		la_vstring_destroy(vstr, true);
	}
	la_proto_tree_destroy(node);
}
#endif

static void printmsg(acarsmsg_t * msg, int chn, struct timeval tv)
{
	oooi_t oooi;

#if defined (WITH_RTL) || defined (WITH_AIR)
	if (inmode >= 3)
		fprintf(fdout, "\n[#%1d (F:%3.3f L:%+3d E:%1d) ", chn + 1,
			channel[chn].Fr / 1000000.0, msg->lvl, msg->err);
	else
#endif
		fprintf(fdout, "\n[#%1d (L:%+3d E:%1d) ", chn + 1, msg->lvl, msg->err);

	if (inmode != 2)
		printdate(tv);

	fprintf(fdout, " --------------------------------\n");
	fprintf(fdout, "Mode : %1c ", msg->mode);
	fprintf(fdout, "Label : %2s ", msg->label);

	if(msg->bid) {
		fprintf(fdout, "Id : %1c ", msg->bid);
		if(msg->ack==0x15) fprintf(fdout, "Nak\n"); else fprintf(fdout, "Ack : %1c\n", msg->ack);
		fprintf(fdout, "Aircraft reg: %s ", msg->addr);
		if(msg->bid >= '0' && msg->bid <= '9') {
			fprintf(fdout, "Flight id: %s\n", msg->fid);
			fprintf(fdout, "No: %4s", msg->no);
		}
	}

	fprintf(fdout, "\n");
	if(msg->txt[0]) fprintf(fdout, "%s\n", msg->txt);
	if (msg->be == 0x17) fprintf(fdout, "ETB\n");

	if(DecodeLabel(msg,&oooi)) {
		fprintf(fdout, "##########################\n");
		if(oooi.da[0]) fprintf(fdout,"Destination Airport : %s\n",oooi.da);
        	if(oooi.sa[0]) fprintf(fdout,"Departure Airport : %s\n",oooi.sa);
        	if(oooi.eta[0]) fprintf(fdout,"Estimation Time of Arrival : %s\n",oooi.eta);
        	if(oooi.gout[0]) fprintf(fdout,"Gate out Time : %s\n",oooi.gout);
        	if(oooi.gin[0]) fprintf(fdout,"Gate in Time : %s\n",oooi.gin);
        	if(oooi.woff[0]) fprintf(fdout,"Wheels off Tme : %s\n",oooi.woff);
        	if(oooi.won[0]) fprintf(fdout,"Wheels on Time : %s\n",oooi.won);
	}
#ifdef HAVE_LIBACARS
	decode_and_print_acars_apps(msg);
#endif

	fflush(fdout);
}


static int buildjson(acarsmsg_t * msg, int chn, struct timeval tv)
{

	oooi_t oooi;
#if defined (WITH_RTL) || defined (WITH_AIR)
	float freq = channel[chn].Fr / 1000000.0;
#else
	float freq = 0;
#endif
	cJSON *json_obj;
	int ok = 0;
	char convert_tmp[8];

	json_obj = cJSON_CreateObject();
	if (json_obj == NULL)
		return ok;

	double t = (double)tv.tv_sec + ((double)tv.tv_usec)/1e6;
	cJSON_AddNumberToObject(json_obj, "timestamp", t);
	if(idstation[0]) cJSON_AddStringToObject(json_obj, "station_id", idstation);
	cJSON_AddNumberToObject(json_obj, "channel", chn);
	snprintf(convert_tmp, sizeof(convert_tmp), "%3.3f", freq);
	cJSON_AddRawToObject(json_obj, "freq", convert_tmp);
	cJSON_AddNumberToObject(json_obj, "level", msg->lvl);
	cJSON_AddNumberToObject(json_obj, "error", msg->err);
	snprintf(convert_tmp, sizeof(convert_tmp), "%c", msg->mode);
	cJSON_AddStringToObject(json_obj, "mode", convert_tmp);
	cJSON_AddStringToObject(json_obj, "label", msg->label);

	if(msg->bid) {
		snprintf(convert_tmp, sizeof(convert_tmp), "%c", msg->bid);
		cJSON_AddStringToObject(json_obj, "block_id", convert_tmp);

		if(msg->ack == 0x15) {
			cJSON_AddFalseToObject(json_obj, "ack");
		} else {
			snprintf(convert_tmp, sizeof(convert_tmp), "%c", msg->ack);
			cJSON_AddStringToObject(json_obj, "ack", convert_tmp);
		}

		cJSON_AddStringToObject(json_obj, "tail", msg->addr);
		if(msg->bid >= '0' && msg->bid <= '9') {
			cJSON_AddStringToObject(json_obj, "flight", msg->fid);
			cJSON_AddStringToObject(json_obj, "msgno", msg->no);
		}
	}
	if(msg->txt[0])
		cJSON_AddStringToObject(json_obj, "text", msg->txt);

	if (msg->be == 0x17)
		cJSON_AddTrueToObject(json_obj, "end");

	if(DecodeLabel(msg, &oooi)) {
		if(oooi.sa[0])
			cJSON_AddStringToObject(json_obj, "depa", oooi.sa);
		if(oooi.da[0])
			cJSON_AddStringToObject(json_obj, "dsta", oooi.da);
		if(oooi.eta[0])
			cJSON_AddStringToObject(json_obj, "eta", oooi.eta);
		if(oooi.gout[0])
			cJSON_AddStringToObject(json_obj, "gtout", oooi.gout);
		if(oooi.gin[0])
			cJSON_AddStringToObject(json_obj, "gtin", oooi.gin);
		if(oooi.woff[0])
			cJSON_AddStringToObject(json_obj, "wloff", oooi.woff);
		if(oooi.won[0])
			cJSON_AddStringToObject(json_obj, "wlin", oooi.won);
	}
	ok = cJSON_PrintPreallocated(json_obj, jsonbuf, JSONBUFLEN, 0);
	cJSON_Delete(json_obj);
	return ok;
}


static void printoneline(acarsmsg_t * msg, int chn, struct timeval tv)
{
	char txt[30];
	char *pstr;

	strncpy(txt, msg->txt, 29);
	txt[29] = 0;
	for (pstr = txt; *pstr != 0; pstr++)
		if (*pstr == '\n' || *pstr == '\r')
			*pstr = ' ';

	fprintf(fdout, "#%1d (L:%+3d E:%1d) ", chn + 1, msg->lvl, msg->err);

	if (inmode != 2)
		printdate(tv);
	fprintf(fdout, " %7s %6s %1c %2s %4s ", msg->addr, msg->fid, msg->mode, msg->label, msg->no);
	fprintf(fdout, "%s", txt);
	fprintf(fdout, "\n");
	fflush(fdout);
}

typedef struct flight_s flight_t;
struct flight_s {
	flight_t *next;
	char addr[8];
	char fid[7];
	struct timeval ts,tl;
	int chm;
	int nbm;
	int rt;
	oooi_t oooi;
};
static flight_t  *flight_head=NULL;

static  flight_t *addFlight(acarsmsg_t * msg, int chn, struct timeval tv)
{
	flight_t *fl,*fld,*flp;
	oooi_t oooi;

	fl=flight_head;
	flp=NULL;
	while(fl) {
		if(strcmp(msg->addr,fl->addr)==0) break;
		flp=fl;
		fl=fl->next;
	}

	if(fl==NULL) {
		fl=calloc(1,sizeof(flight_t));
		strncpy(fl->addr,msg->addr,8);
		fl->nbm=0;
		fl->ts=tv;
		fl->chm=0;
		fl->rt=0;
		fl->next=NULL;
	}

	strncpy(fl->fid,msg->fid,7);
	fl->tl=tv;
	fl->chm|=(1<<chn);
	fl->nbm+=1;

	if(DecodeLabel(msg,&oooi)) {
		if(oooi.da[0]) memcpy(fl->oooi.da,oooi.da,5);
        	if(oooi.sa[0]) memcpy(fl->oooi.sa,oooi.sa,5);
        	if(oooi.eta[0]) memcpy(fl->oooi.eta,oooi.eta,5);
        	if(oooi.gout[0]) memcpy(fl->oooi.gout,oooi.gout,5);
        	if(oooi.gin[0]) memcpy(fl->oooi.gin,oooi.gin,5);
        	if(oooi.woff[0]) memcpy(fl->oooi.woff,oooi.woff,5);
        	if(oooi.won[0]) memcpy(fl->oooi.won,oooi.won,5);
	}

	if(flp) {
		flp->next=fl->next;
		fl->next=flight_head;
	}
	flight_head=fl;

	flp=NULL;fld=fl;
	while(fld) {
		if(fld->tl.tv_sec<(tv.tv_sec-mdly)) {
			if(flp) {
				flp->next=fld->next;
				free(fld);
				fld=flp->next;
			} else {
				flight_head=fld->next;
				free(fld);
				fld=flight_head;
			}
		} else {
			flp=fld;
			fld=fld->next;
		}
	}

	return(fl);
}

static int routejson(flight_t *fl,struct timeval tv)
{
  if(fl==NULL)
	return 0;

  if(fl->rt==0 && fl->fid[0] && fl->oooi.sa[0] && fl->oooi.da[0]) {

	cJSON *json_obj;
	int ok;

	json_obj = cJSON_CreateObject();
	if (json_obj == NULL)
		return 0;

	double t = (double)tv.tv_sec + ((double)tv.tv_usec)/1e6;
	cJSON_AddNumberToObject(json_obj, "timestamp", t);
	if(idstation[0]) cJSON_AddStringToObject(json_obj, "station_id", idstation);
	cJSON_AddStringToObject(json_obj, "flight", fl->fid);
	cJSON_AddStringToObject(json_obj, "depa", fl->oooi.sa);
	cJSON_AddStringToObject(json_obj, "dsta", fl->oooi.da);

	ok = cJSON_PrintPreallocated(json_obj, jsonbuf, JSONBUFLEN, 0);
	cJSON_Delete(json_obj);
	
	fl->rt=ok;
	return ok;
 } else
	return 0;
}

static void printmonitor(acarsmsg_t * msg, int chn, struct timeval tv)
{
	flight_t *fl;

	cls();

	printf("             Acarsdec monitor "); printtime(tv);
	printf("\n Aircraft Flight   Nb Channels     First    DEP   ARR   ETA\n");

	fl=flight_head;
	while(fl) {
		int i;

		printf(" %-8s %-7s %3d ", fl->addr, fl->fid,fl->nbm);
		for(i=0;i<nbch;i++) printf("%c",(fl->chm&(1<<i))?'x':'.');
		for(;i<MAXNBCHANNELS;i++) printf(" ");
		printf(" "); printtime(fl->ts);
        	if(fl->oooi.sa[0]) printf(" %4s ",fl->oooi.sa); else printf("      ");
		if(fl->oooi.da[0]) printf(" %4s ",fl->oooi.da); else printf("      ");
        	if(fl->oooi.eta[0]) printf(" %4s ",fl->oooi.eta); else printf("      ");
		printf("\n");

		fl=fl->next;
	}

	fflush(stdout);
}

void outputmsg(const msgblk_t * blk)
{
	acarsmsg_t msg;
	int i, j, k;
	int jok=0;
	int outflg=0;
	flight_t *fl=NULL;

	/* fill msg struct */
	msg.lvl = blk->lvl;
	msg.err = blk->err;

	k = 0;
	msg.mode = blk->txt[k];
	k++;

        for (i = 0, j = 0; i < 7; i++, k++) {
                if (blk->txt[k] != '.') {
                        msg.addr[j] = blk->txt[k];
                        j++;
                }
        }
        msg.addr[j] = '\0';

	/* ACK/NAK */
	msg.ack = blk->txt[k];
	k++;

	msg.label[0] = blk->txt[k];
	k++;
	msg.label[1] = blk->txt[k];
	if(msg.label[1]==0x7f) msg.label[1]='d';
	k++;
	msg.label[2] = '\0';

	msg.bid = blk->txt[k];
	k++;

	/* txt start  */
	msg.bs = blk->txt[k];
	k++;

	msg.no[0] = '\0';
	msg.fid[0] = '\0';
	msg.txt[0] = '\0';

	if (airflt && !(msg.bid >= '0' && msg.bid <= '9'))
		return;

	if (msg.bs != 0x03) {
		if (msg.bid >= '0' && msg.bid <= '9') {
			/* message no */
			for (i = 0; i < 4 && k < blk->len - 1; i++, k++) {
				msg.no[i] = blk->txt[k];
			}
			msg.no[i] = '\0';

			/* Flight id */
			for (i = 0; i < 6 && k < blk->len - 1; i++, k++) {
				msg.fid[i] = blk->txt[k];
			}
			msg.fid[i] = '\0';

			outflg=1;
		}

		/* Message txt */
		for (i = 0; k < blk->len - 1; i++, k++)
			msg.txt[i] = blk->txt[k];
		msg.txt[i] = 0;
	}

	/* txt end */
	msg.be = blk->txt[blk->len - 1];


	if(label_filter(msg.label)==0) return;

	if(outflg)
		fl=addFlight(&msg,blk->chn,blk->tv);

	if(jsonbuf) {
		if(outtype == OUTTYPE_ROUTEJSON )
			jok=routejson(fl,blk->tv);
		else
			jok=buildjson(&msg, blk->chn, blk->tv);
	}

	if((hourly || daily) && outtype != OUTTYPE_NONE && rotate_outfile() < 0) {
		_exit(1);
	}
	switch (outtype) {
	case OUTTYPE_NONE:
		break;
	case OUTTYPE_ONELINE:
		printoneline(&msg, blk->chn, blk->tv);
		break;
	case OUTTYPE_STD:
		printmsg(&msg, blk->chn, blk->tv);
		break;
	case OUTTYPE_MONITOR:
		printmonitor(&msg, blk->chn, blk->tv);
		break;
	case OUTTYPE_ROUTEJSON:
	case OUTTYPE_JSON:
		if(jok) {
			fprintf(fdout, "%s\n", jsonbuf);
			fflush(fdout);
		}
		break;
	}

	if (sockfd > 0) {
		switch (netout) {
		case NETLOG_PLANEPLOTTER:
			outpp(&msg);
			break;
		case NETLOG_NATIVE:
			outsv(&msg, blk->chn, blk->tv);
			break;
		case NETLOG_JSON:
			if(jok) outjson();
			break;
		}
	}
}

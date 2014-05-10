#ifndef __CW_H_
#define __CW_H_
#include <stdint.h>
#include <zmq.h>
#include <syslog.h> /* for LOG_* constants */

/*
     ######  ####  ######   ##    ##    ###    ##        ######
    ##    ##  ##  ##    ##  ###   ##   ## ##   ##       ##    ##
    ##        ##  ##        ####  ##  ##   ##  ##       ##
     ######   ##  ##   #### ## ## ## ##     ## ##        ######
          ##  ##  ##    ##  ##  #### ######### ##             ##
    ##    ##  ##  ##    ##  ##   ### ##     ## ##       ##    ##
     ######  ####  ######   ##    ## ##     ## ########  ######
 */
void cw_sig_catch(void);
int cw_sig_interrupt(void);

/*
    ##       ####  ######  ########  ######
    ##        ##  ##    ##    ##    ##    ##
    ##        ##  ##          ##    ##
    ##        ##   ######     ##     ######
    ##        ##        ##    ##          ##
    ##        ##  ##    ##    ##    ##    ##
    ######## ####  ######     ##     ######
 */

typedef struct cw_list cw_list_t;
struct cw_list {
	cw_list_t *next;
	cw_list_t *prev;
};

#define LIST(n) cw_list_t n = { &(n), &(n) }
#define cw_list_object(l,t,m) ((t*)((char*)(l) - offsetof(t,m)))
#define cw_list_head(l,t,m) cw_list_object((l)->next, t, m)
#define cw_list_tail(l,t,m) cw_list_object((l)->prev, t, m)

/** Iterate over a list */
#define for_each(pos, head) \
	for (pos = (head)->next; \
	     pos != (head);      \
	     pos = pos->next)

/** Iterate over a list (safely) */
#define for_each_safe(pos, tmp, head) \
	for (pos = (head)->next, tmp = pos->next; \
	     pos != (head);                       \
	     pos = tmp, tmp = pos->next)

#define cw_list_next(o,m) cw_list_object(o->m.next, typeof(*o), m)

/** Iterate over a list, accessing the objects */
#define for_each_object(pos, head, memb) \
	for (pos = cw_list_object((head)->next, typeof(*pos), memb); \
	     &pos->memb != (head);                                 \
	     pos = cw_list_object(pos->memb.next, typeof(*pos), memb))

/** Iterate over a list (safely), accessing the objects */
#define for_each_object_safe(pos, tmp, head, memb) \
	for (pos = cw_list_object((head)->next, typeof(*pos), memb),   \
	     tmp = cw_list_object(pos->memb.next, typeof(*pos), memb); \
	     &pos->memb != (head);                                   \
	     pos = tmp, tmp = cw_list_object(tmp->memb.next, typeof(*tmp), memb))

int cw_list_init(cw_list_t *l);

int cw_list_isempty(cw_list_t *l);
size_t cw_list_len(cw_list_t *l);

int cw_list_splice(cw_list_t *prev, cw_list_t *next);
int cw_list_delete(cw_list_t *n);

int cw_list_unshift(cw_list_t *l, cw_list_t *n);
int cw_list_push   (cw_list_t *l, cw_list_t *n);

cw_list_t* cw_list_shift(cw_list_t *l);
cw_list_t* cw_list_pop  (cw_list_t *l);

/*
     ######  ######## ########  #### ##    ##  ######    ######
    ##    ##    ##    ##     ##  ##  ###   ## ##    ##  ##    ##
    ##          ##    ##     ##  ##  ####  ## ##        ##
     ######     ##    ########   ##  ## ## ## ##   ####  ######
          ##    ##    ##   ##    ##  ##  #### ##    ##        ##
    ##    ##    ##    ##    ##   ##  ##   ### ##    ##  ##    ##
     ######     ##    ##     ## #### ##    ##  ######    ######
 */

/*
    ######## #### ##     ## ########
       ##     ##  ###   ### ##
       ##     ##  #### #### ##
       ##     ##  ## ### ## ######
       ##     ##  ##     ## ##
       ##     ##  ##     ## ##
       ##    #### ##     ## ########
 */

int32_t cw_time_s(void);
int64_t cw_time_ms(void);
int cw_sleep_ms(int64_t ms);

/*
    ########     ###    ##    ## ########
    ##     ##   ## ##   ###   ## ##     ##
    ##     ##  ##   ##  ####  ## ##     ##
    ########  ##     ## ## ## ## ##     ##
    ##   ##   ######### ##  #### ##     ##
    ##    ##  ##     ## ##   ### ##     ##
    ##     ## ##     ## ##    ## ########
 */

void cw_srand(void);

/*
    ######## ##     ##  #######
         ##  ###   ### ##     ##
        ##   #### #### ##     ##
       ##    ## ### ## ##     ##
      ##     ##     ## ##  ## ##
     ##      ##     ## ##    ##
    ######## ##     ##  ##### ##
 */

typedef struct cw_frame cw_frame_t;
struct cw_frame {
	cw_list_t  l;
	zmq_msg_t   msg;

	size_t      size;
	uint8_t    *data;

	int         more;
	int         binary;
};

void * cw_zmq_ident(void *zocket, void *id);
cw_frame_t *cw_frame_recv(void *zocket);
int cw_frame_send(void *zocket, cw_frame_t *f);
cw_frame_t *cw_frame_new(const char *s);
cw_frame_t *cw_frame_copy(cw_frame_t *f);
void cw_frame_close(cw_frame_t *f);
char *cw_frame_text(cw_frame_t *f);
char *cw_frame_hex(cw_frame_t *f);
int cw_frame_is(cw_frame_t *f, const char *s);
int cw_frame_cmp(cw_frame_t *a, cw_frame_t *b);
void cw_frame_dump(FILE *io, cw_frame_t *f);

zmq_msg_t * cw_zmq_msg_recv(void *zocket, zmq_msg_t *msg, int *more);
void cw_zmq_shutdown(void *zocket, int linger);

typedef struct cw_pdu cw_pdu_t;
struct cw_pdu {
	cw_frame_t *src;

	char      *client;
	char      *type;
	char      *data;

	int        len;
	cw_list_t frames;
};

int cw_pdu_init(cw_pdu_t *pdu);
void cw_pdu_destroy(cw_pdu_t *pdu);
void cw_pdu_dump(FILE *io, cw_pdu_t *pdu);

cw_frame_t * cw_pdu_frame(cw_pdu_t *pdu, size_t n);
#define cw_pdu_text(pdu,n) cw_frame_text(cw_pdu_frame(pdu,n))

cw_pdu_t *cw_pdu_recv(void *zocket);

cw_pdu_t *cw_pdu_make(cw_frame_t *dest, int n, ...);
int cw_pdu_extend(cw_pdu_t *pdu, cw_frame_t *f);

int cw_pdu_send(void *zocket, cw_pdu_t *pdu);

/*
     ######   #######  ##    ## ######## ####  ######
    ##    ## ##     ## ###   ## ##        ##  ##    ##
    ##       ##     ## ####  ## ##        ##  ##
    ##       ##     ## ## ## ## ######    ##  ##   ####
    ##       ##     ## ##  #### ##        ##  ##    ##
    ##    ## ##     ## ##   ### ##        ##  ##    ##
     ######   #######  ##    ## ##       ####  ######
 */

typedef struct cw_keyval cw_keyval_t;
struct cw_keyval {
	char *key;
	char *val;
	cw_list_t l;
};

int cw_cfg_set(cw_list_t *cfg, const char *key, const char *val);
char * cw_cfg_get(cw_list_t *cfg, const char *key);
int cw_cfg_isset(cw_list_t *cfg, const char *key);
int cw_cfg_read(cw_list_t *cfg, FILE *io);
int cw_cfg_uniq(cw_list_t *dest, cw_list_t *src);


/*
    ##        #######   ######    ######
    ##       ##     ## ##    ##  ##    ##
    ##       ##     ## ##        ##
    ##       ##     ## ##   ####  ######
    ##       ##     ## ##    ##        ##
    ##       ##     ## ##    ##  ##    ##
    ########  #######   ######    ######
 */

void cw_log_open(const char *ident, const char *facility);
void cw_log_close(void);
int cw_log_level(int level, const char *name);
void cw_log(int level, const char *fmt, ...);

/*
    #######   ##     ## ##    ##
    ##    ##  ##     ## ###   ##
    ##    ##  ##     ## ####  ##
    #######   ##     ## ## ## ##
    ##   ##   ##     ## ##  ####
    ##    ##  ##     ## ##   ###
    ##     ##  #######  ##    ##
*/

int cw_run(char *cmd, ...);

/*
    ########     ###    ######## ##     ##  #######  ##    ##
    ##     ##   ## ##   ##       ###   ### ##     ## ###   ##
    ##     ##  ##   ##  ##       #### #### ##     ## ####  ##
    ##     ## ##     ## ######   ## ### ## ##     ## ## ## ##
    ##     ## ######### ##       ##     ## ##     ## ##  ####
    ##     ## ##     ## ##       ##     ## ##     ## ##   ###
    ########  ##     ## ######## ##     ##  #######  ##    ##
 */

int cw_daemonize(const char *pidfile, const char *user, const char *group);
int cw_cleanenv(int n, const char **keep);

#endif

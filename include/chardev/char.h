#ifndef QEMU_CHAR_H
#define QEMU_CHAR_H

#include "qapi/qapi-types-char.h"
#include "qemu/main-loop.h"
#include "qemu/bitmap.h"
#include "qom/object.h"

/* Included for the SocketChardev struct definition */
#include "io/channel.h"
#include "io/channel-socket.h"
#include "io/net-listener.h"
#include "crypto/tlscreds.h"

#define IAC_EOR 239
#define IAC_SE 240
#define IAC_NOP 241
#define IAC_BREAK 243
#define IAC_IP 244
#define IAC_SB 250
#define IAC 255

#define SOCKET_CHARDEV(obj)                                     \
    OBJECT_CHECK(SocketChardev, (obj), TYPE_CHARDEV_SOCKET)

/* character device */
typedef struct CharBackend CharBackend;

typedef enum {
    CHR_EVENT_BREAK, /* serial break char */
    CHR_EVENT_OPENED, /* new connection established */
    CHR_EVENT_MUX_IN, /* mux-focus was set to this terminal */
    CHR_EVENT_MUX_OUT, /* mux-focus will move on */
    CHR_EVENT_CLOSED /* connection closed.  NOTE: currently this event
                      * is only bound to the read port of the chardev.
                      * Normally the read port and write port of a
                      * chardev should be the same, but it can be
                      * different, e.g., for fd chardevs, when the two
                      * fds are different.  So when we received the
                      * CLOSED event it's still possible that the out
                      * port is still open.  TODO: we should only send
                      * the CLOSED event when both ports are closed.
                      */
} QEMUChrEvent;

#define CHR_READ_BUF_LEN 4096

typedef enum {
    /* Whether the chardev peer is able to close and
     * reopen the data channel, thus requiring support
     * for qemu_chr_wait_connected() to wait for a
     * valid connection */
    QEMU_CHAR_FEATURE_RECONNECTABLE,
    /* Whether it is possible to send/recv file descriptors
     * over the data channel */
    QEMU_CHAR_FEATURE_FD_PASS,
    /* Whether replay or record mode is enabled */
    QEMU_CHAR_FEATURE_REPLAY,
    /* Whether the gcontext can be changed after calling
     * qemu_chr_be_update_read_handlers() */
    QEMU_CHAR_FEATURE_GCONTEXT,

    QEMU_CHAR_FEATURE_LAST,
} ChardevFeature;

#define qemu_chr_replay(chr) qemu_chr_has_feature(chr, QEMU_CHAR_FEATURE_REPLAY)

struct Chardev {
    Object parent_obj;

    QemuMutex chr_write_lock;
    CharBackend *be;
    char *label;
    char *filename;
    int logfd;
    int be_open;
    GSource *gsource;
    GMainContext *gcontext;
    DECLARE_BITMAP(features, QEMU_CHAR_FEATURE_LAST);
};

typedef struct {
    char buf[21];
    size_t buflen;
} TCPChardevTelnetInit;

typedef enum {
    TCP_CHARDEV_STATE_DISCONNECTED,
    TCP_CHARDEV_STATE_CONNECTING,
    TCP_CHARDEV_STATE_CONNECTED,
} TCPChardevState;

typedef struct {
    Chardev parent;
    QIOChannel *ioc; /* Client I/O channel */
    QIOChannelSocket *sioc; /* Client master channel */
    QIONetListener *listener;
    GSource *hup_source;
    QCryptoTLSCreds *tls_creds;
    char *tls_authz;
    TCPChardevState state;
    int max_size;
    int do_telnetopt;
    int do_nodelay;
    int *read_msgfds;
    size_t read_msgfds_num;
    int *write_msgfds;
    size_t write_msgfds_num;

    SocketAddress *addr;
    bool is_listen;
    bool is_telnet;
    bool is_tn3270;
    GSource *telnet_source;
    TCPChardevTelnetInit *telnet_init;

    bool is_websock;

    int64_t timeout;
    GSource *reconnect_timer;
    int64_t reconnect_time;
    bool connect_err_reported;

    QIOTask *connect_task;
} SocketChardev;

/**
 * qemu_chr_new_from_opts:
 * @opts: see qemu-config.c for a list of valid options
 * @context: the #GMainContext to be used at initialization time
 *
 * Create a new character backend from a QemuOpts list.
 *
 * Returns: on success: a new character backend
 *          otherwise:  NULL; @errp specifies the error
 *                            or left untouched in case of help option
 */
Chardev *qemu_chr_new_from_opts(QemuOpts *opts,
                                GMainContext *context,
                                Error **errp);

/**
 * qemu_chr_parse_common:
 * @opts: the options that still need parsing
 * @backend: a new backend
 *
 * Parse the common options available to all character backends.
 */
void qemu_chr_parse_common(QemuOpts *opts, ChardevCommon *backend);

/**
 * qemu_chr_parse_opts:
 *
 * Parse the options to the ChardevBackend struct.
 *
 * Returns: a new backend or NULL on error
 */
ChardevBackend *qemu_chr_parse_opts(QemuOpts *opts,
                                    Error **errp);

/**
 * qemu_chr_new:
 * @label: the name of the backend
 * @filename: the URI
 * @context: the #GMainContext to be used at initialization time
 *
 * Create a new character backend from a URI.
 * Do not implicitly initialize a monitor if the chardev is muxed.
 *
 * Returns: a new character backend
 */
Chardev *qemu_chr_new(const char *label, const char *filename,
                      GMainContext *context);

/**
 * qemu_chr_new_mux_mon:
 * @label: the name of the backend
 * @filename: the URI
 * @context: the #GMainContext to be used at initialization time
 *
 * Create a new character backend from a URI.
 * Implicitly initialize a monitor if the chardev is muxed.
 *
 * Returns: a new character backend
 */
Chardev *qemu_chr_new_mux_mon(const char *label, const char *filename,
                              GMainContext *context);

/**
* qemu_chr_change:
* @opts: the new backend options
 *
 * Change an existing character backend
 */
void qemu_chr_change(QemuOpts *opts, Error **errp);

/**
 * qemu_chr_cleanup:
 *
 * Delete all chardevs (when leaving qemu)
 */
void qemu_chr_cleanup(void);

/**
 * qemu_chr_new_noreplay:
 * @label: the name of the backend
 * @filename: the URI
 * @permit_mux_mon: if chardev is muxed, initialize a monitor
 * @context: the #GMainContext to be used at initialization time
 *
 * Create a new character backend from a URI.
 * Character device communications are not written
 * into the replay log.
 *
 * Returns: a new character backend
 */
Chardev *qemu_chr_new_noreplay(const char *label, const char *filename,
                               bool permit_mux_mon, GMainContext *context);

/**
 * qemu_chr_be_can_write:
 *
 * Determine how much data the front end can currently accept.  This function
 * returns the number of bytes the front end can accept.  If it returns 0, the
 * front end cannot receive data at the moment.  The function must be polled
 * to determine when data can be received.
 *
 * Returns: the number of bytes the front end can receive via @qemu_chr_be_write
 */
int qemu_chr_be_can_write(Chardev *s);

/**
 * qemu_chr_be_write:
 * @buf: a buffer to receive data from the front end
 * @len: the number of bytes to receive from the front end
 *
 * Write data from the back end to the front end.  Before issuing this call,
 * the caller should call @qemu_chr_be_can_write to determine how much data
 * the front end can currently accept.
 */
void qemu_chr_be_write(Chardev *s, uint8_t *buf, int len);

/**
 * qemu_chr_be_write_impl:
 * @buf: a buffer to receive data from the front end
 * @len: the number of bytes to receive from the front end
 *
 * Implementation of back end writing. Used by replay module.
 */
void qemu_chr_be_write_impl(Chardev *s, uint8_t *buf, int len);

/**
 * qemu_chr_be_update_read_handlers:
 * @context: the gcontext that will be used to attach the watch sources
 *
 * Invoked when frontend read handlers are setup
 */
void qemu_chr_be_update_read_handlers(Chardev *s,
                                      GMainContext *context);

/**
 * qemu_chr_be_event:
 * @event: the event to send
 *
 * Send an event from the back end to the front end.
 */
void qemu_chr_be_event(Chardev *s, int event);

int qemu_chr_add_client(Chardev *s, int fd);
Chardev *qemu_chr_find(const char *name);

bool qemu_chr_has_feature(Chardev *chr,
                          ChardevFeature feature);
void qemu_chr_set_feature(Chardev *chr,
                          ChardevFeature feature);
QemuOpts *qemu_chr_parse_compat(const char *label, const char *filename,
                                bool permit_mux_mon);
int qemu_chr_write(Chardev *s, const uint8_t *buf, int len, bool write_all);
#define qemu_chr_write_all(s, buf, len) qemu_chr_write(s, buf, len, true)
int qemu_chr_wait_connected(Chardev *chr, Error **errp);

#define TYPE_CHARDEV "chardev"
#define CHARDEV(obj) OBJECT_CHECK(Chardev, (obj), TYPE_CHARDEV)
#define CHARDEV_CLASS(klass) \
    OBJECT_CLASS_CHECK(ChardevClass, (klass), TYPE_CHARDEV)
#define CHARDEV_GET_CLASS(obj) \
    OBJECT_GET_CLASS(ChardevClass, (obj), TYPE_CHARDEV)

#define TYPE_CHARDEV_NULL "chardev-null"
#define TYPE_CHARDEV_MUX "chardev-mux"
#define TYPE_CHARDEV_RINGBUF "chardev-ringbuf"
#define TYPE_CHARDEV_PTY "chardev-pty"
#define TYPE_CHARDEV_CONSOLE "chardev-console"
#define TYPE_CHARDEV_STDIO "chardev-stdio"
#define TYPE_CHARDEV_PIPE "chardev-pipe"
#define TYPE_CHARDEV_MEMORY "chardev-memory"
#define TYPE_CHARDEV_PARALLEL "chardev-parallel"
#define TYPE_CHARDEV_FILE "chardev-file"
#define TYPE_CHARDEV_SERIAL "chardev-serial"
#define TYPE_CHARDEV_SOCKET "chardev-socket"
#define TYPE_CHARDEV_UDP "chardev-udp"

#define CHARDEV_IS_RINGBUF(chr) \
    object_dynamic_cast(OBJECT(chr), TYPE_CHARDEV_RINGBUF)
#define CHARDEV_IS_PTY(chr) \
    object_dynamic_cast(OBJECT(chr), TYPE_CHARDEV_PTY)

typedef struct ChardevClass {
    ObjectClass parent_class;

    bool internal; /* TODO: eventually use TYPE_USER_CREATABLE */
    void (*parse)(QemuOpts *opts, ChardevBackend *backend, Error **errp);

    void (*open)(Chardev *chr, ChardevBackend *backend,
                 bool *be_opened, Error **errp);

    int (*chr_write)(Chardev *s, const uint8_t *buf, int len);
    int (*chr_sync_read)(Chardev *s, const uint8_t *buf, int len);
    GSource *(*chr_add_watch)(Chardev *s, GIOCondition cond);
    void (*chr_update_read_handler)(Chardev *s);
    int (*chr_ioctl)(Chardev *s, int cmd, void *arg);
    int (*get_msgfds)(Chardev *s, int* fds, int num);
    int (*set_msgfds)(Chardev *s, int *fds, int num);
    int (*chr_add_client)(Chardev *chr, int fd);
    int (*chr_wait_connected)(Chardev *chr, Error **errp);
    void (*chr_disconnect)(Chardev *chr);
    void (*chr_accept_input)(Chardev *chr);
    void (*chr_set_echo)(Chardev *chr, bool echo);
    void (*chr_set_fe_open)(Chardev *chr, int fe_open);
    void (*chr_be_event)(Chardev *s, int event);
    /* Return 0 if succeeded, 1 if failed */
    int (*chr_machine_done)(Chardev *chr);
} ChardevClass;

Chardev *qemu_chardev_new(const char *id, const char *typename,
                          ChardevBackend *backend, GMainContext *context,
                          Error **errp);

extern int term_escape_char;

GSource *qemu_chr_timeout_add_ms(Chardev *chr, guint ms,
                                 GSourceFunc func, void *private);

/* console.c */
void qemu_chr_parse_vc(QemuOpts *opts, ChardevBackend *backend, Error **errp);

ssize_t tcp_chr_recv(Chardev *chr, char *buf, size_t len);

#endif

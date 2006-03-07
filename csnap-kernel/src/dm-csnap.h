#define PACKED __attribute__ ((packed))
#define MAGIC  0xadbe

struct head { uint32_t code; uint32_t length; } PACKED;

enum csnap_codes
{
	REPLY_ERROR = 0xbead0000,
	IDENTIFY,
	REPLY_IDENTIFY,
	QUERY_WRITE,
	REPLY_ORIGIN_WRITE,
	REPLY_SNAPSHOT_WRITE,
	QUERY_SNAPSHOT_READ,
	REPLY_SNAPSHOT_READ,
	REPLY_SNAPSHOT_READ_ORIGIN,
	FINISH_SNAPSHOT_READ,
	CREATE_SNAPSHOT,
	REPLY_CREATE_SNAPSHOT,
	DELETE_SNAPSHOT,
	REPLY_DELETE_SNAPSHOT,
	DUMP_TREE,
	INITIALIZE_SNAPSTORE,
	NEED_SERVER,
	CONNECT_SERVER,
	REPLY_CONNECT_SERVER,
	CONTROL_SOCKET,
	SERVER_READY,
	START_SERVER,
	SHUTDOWN_SERVER,
	SET_IDENTITY,
	UPLOAD_LOCK,
	FINISH_UPLOAD_LOCK,
	NEED_CLIENTS,
	UPLOAD_CLIENT_ID,
	FINISH_UPLOAD_CLIENT_ID,
	REMOVE_CLIENT_IDS,
};

struct match_id { uint64_t id; uint64_t mask; } PACKED;
struct set_id { uint64_t id; } PACKED;
struct identify { uint64_t id; int32_t snap; } PACKED;
struct create_snapshot { uint32_t snap; } PACKED;

typedef uint16_t shortcount; /* !!! what is this all about */

struct rw_request
{
	uint16_t id;
	shortcount count;
	struct chunk_range
	{
		uint64_t chunk;
		shortcount chunks;
	} PACKED ranges[];
} PACKED;

/* !!! can there be only one flavor of me please */
struct rw_request1
{
	uint16_t id;
	shortcount count;
	struct chunk_range PACKED ranges[1];
} PACKED;

/* decruft me... !!! */
#define maxbody 500
struct rwmessage { struct head head; struct rw_request body; };
struct messagebuf { struct head head; char body[maxbody]; };
/* ...decruft me */

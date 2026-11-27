/* M480Nico - tiny JSON parser (arena allocated DOM) */
#ifndef M480_JSON_H
#define M480_JSON_H

enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ };

typedef struct JNode {
	char type;
	char bval;
	const char *key;     /* member name when inside an object */
	const char *str;     /* J_STR (UTF-8), J_NUM (raw text) */
	struct JNode *child, *next;
} JNode;

typedef struct JDoc JDoc;

JDoc  *json_parse(const char *text, int len);  /* NULL on failure */
JNode *json_root(JDoc *d);
void   json_free(JDoc *d);

JNode *jget(JNode *obj, const char *key);       /* object member */
JNode *jidx(JNode *arr, int i);                 /* array element */
/* path like "a.b.0.c" (numbers index arrays) */
JNode *jpath(JNode *n, const char *path);
const char *jstr(JNode *n);                     /* string value or NULL */
const char *jpstr(JNode *n, const char *path);  /* jstr(jpath()) */
long   jnum(JNode *n, long def);
/* depth first search: calls fn for every object member with this key */
void   jfind(JNode *n, const char *key, void (*fn)(JNode *val, void *ctx), void *ctx);

#endif

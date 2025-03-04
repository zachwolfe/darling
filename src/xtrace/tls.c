#include <stdlib.h>
#include <darling/emulation/ext/for-xtrace.h>
#include "tls.h"
#include "malloc.h"
#include "lock.h"
#include <darling/emulation/simple.h>
#include <pthread/tsd_private.h>
#include "xtracelib.h"

#ifndef XTRACE_TLS_DEBUG
	#define XTRACE_TLS_DEBUG 0
#endif

#if XTRACE_TLS_DEBUG
	#define xtrace_tls_debug(x, ...) xtrace_log(x "\n", ## __VA_ARGS__)
#else
	#define xtrace_tls_debug(x, ...)
#endif

// 10 TLS vars should be enough, right?
#define TLS_TABLE_MAX_SIZE 10

typedef struct tls_table* tls_table_t;
struct tls_table {
	size_t size;
	void* table[TLS_TABLE_MAX_SIZE][2];
};

// since we still need to handle some calls after pthread_terminate is called and libpthread unwinds its TLS right before calling pthread_terminate,
// we have to use a slightly hackier technique: using one of the system's reserved but unused TLS keys.
// key 200 seems like a good fit; it's in the reserved region but it's not currently listed in `pthread/tsd_private.h` as being in-use by anything.

#define __PTK_XTRACE_TLS 200

// unfortunately, this approach also means that we can't automatically free the TLS memory when the thread dies, since the TLS table needs to stay alive after pthread_terminate.
// in order to clean up the memory without a pthread key destructor, we'd need to modify our libsystem_kernel to inform us (via a hook) in every case where the thread could die.
//
// leaking a bit of memory per-thread being xtrace'd shouldn't be a big problem, unless the tracee is creating and terminating threads very quickly.
// but it'd be nice if this could eventually be fixed (probably by adding a death hook to libsystem_kernel, as described above).

#if 0
static void tls_table_destructor(void* _table) {
	tls_table_t table = _table;
	xtrace_tls_debug("destroying table %p", table);
	for (size_t i = 0; i < table->size; ++i) {
		xtrace_tls_debug("freeing value %p for key %p", table->table[i][1], table->table[i][0]);
		xtrace_free(table->table[i][1]);
	}
	xtrace_tls_debug("freeing table %p", table);
	xtrace_free(table);
};
#endif

void* xtrace_tls(void* key, size_t size, bool* created) {
	xtrace_tls_debug("looking up tls variable for key %p", key);

	tls_table_t table = _pthread_getspecific_direct(__PTK_XTRACE_TLS);

	xtrace_tls_debug("got %p as table pointer from pthread", table);

	// if the table doesn't exist yet, create it
	if (table == NULL) {
		xtrace_tls_debug("table is NULL, creating now...");
		table = xtrace_malloc(sizeof(struct tls_table));
		if (table == NULL) {
			xtrace_abort("xtrace: failed TLS table memory allocation");
		}
		table->size = 0;
		_pthread_setspecific_direct(__PTK_XTRACE_TLS, table);
	}

	// check if the key is already present
	for (size_t i = 0; i < table->size; ++i) {
		if (table->table[i][0] == key) {
			xtrace_tls_debug("found entry in table for key %p with value %p", key, table->table[i][1]);
			if (created) {
				*created = false;
			}
			return table->table[i][1];
		}
	}

	// otherwise, create it
	xtrace_tls_debug("creating new entry in table for key %p", key);
	size_t index = table->size++;
	table->table[index][0] = key;
	table->table[index][1] = xtrace_malloc(size);
	if (table->table[index][1] == NULL) {
		xtrace_abort("xtrace: failed TLS variable memory allocation");
	}
	xtrace_tls_debug("new table entry created for key %p with value %p", key, table->table[index][1]);
	if (created) {
		*created = true;
	}
	return table->table[index][1];
};

/*-
 * Copyright 2016 Vsevolod Stakhov
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "lua_common.h"
#include "libutil/map.h"
#include "libutil/map_private.h"
#include "libutil/radix.h"
#include "lua/lua_map.h"

/***
 * This module is used to manage rspamd maps and map like objects
 *
 * @module rspamd_map
 */

/***
 * @method map:get_key(in)
 * Variable method for different types of maps:
 *
 * - For hash maps it returns boolean and accepts string
 * - For kv maps it returns string (or nil) and accepts string
 * - For radix maps it returns boolean and accepts IP address (as object, string or number)
 *
 * @param {vary} in input to check
 * @return {bool|string} if a value is found then this function returns string or `True` if not - then it returns `nil` or `False`
 */
LUA_FUNCTION_DEF (map, get_key);


/***
 * @method map:is_signed()
 * Returns `True` if a map is signed
 * @return {bool} signed value
 */
LUA_FUNCTION_DEF (map, is_signed);

/***
 * @method map:get_proto()
 * Returns protocol of map as string:
 *
 * - `http`: for HTTP map
 * - `file`: for file map
 * - `embedded`: for manually created maps
 * @return {string} string representation of the map protocol
 */
LUA_FUNCTION_DEF (map, get_proto);

/***
 * @method map:get_sign_key()
 * Returns pubkey used for signing as base32 string or nil
 * @return {string} base32 encoded string or nil
 */
LUA_FUNCTION_DEF (map, get_sign_key);

/***
 * @method map:set_sign_key(key)
 * Set trusted key for signatures for this map
 * @param {string} key base32 encoded string or nil
 */
LUA_FUNCTION_DEF (map, set_sign_key);

/***
 * @method map:set_callback(cb)
 * Set callback for a specified callback map.
 * @param {function} cb map callback function
 */
LUA_FUNCTION_DEF (map, set_callback);

/***
 * @method map:get_uri()
 * Get uri for a specified map
 * @return {string} map's URI
 */
LUA_FUNCTION_DEF (map, get_uri);

static const struct luaL_reg maplib_m[] = {
	LUA_INTERFACE_DEF (map, get_key),
	LUA_INTERFACE_DEF (map, is_signed),
	LUA_INTERFACE_DEF (map, get_proto),
	LUA_INTERFACE_DEF (map, get_sign_key),
	LUA_INTERFACE_DEF (map, set_sign_key),
	LUA_INTERFACE_DEF (map, set_callback),
	LUA_INTERFACE_DEF (map, get_uri),
	{"__tostring", rspamd_lua_class_tostring},
	{NULL, NULL}
};

struct lua_map_callback_data {
	lua_State *L;
	gint ref;
	GString *data;
	struct rspamd_lua_map *lua_map;
};

struct rspamd_lua_map  *
lua_check_map (lua_State * L, gint pos)
{
	void *ud = luaL_checkudata (L, pos, "rspamd{map}");
	luaL_argcheck (L, ud != NULL, pos, "'map' expected");
	return ud ? *((struct rspamd_lua_map **)ud) : NULL;
}

gint
lua_config_add_radix_map (lua_State *L)
{
	struct rspamd_config *cfg = lua_check_config (L, 1);
	const gchar *map_line, *description;
	struct rspamd_lua_map *map, **pmap;
	struct rspamd_map *m;

	if (cfg) {
		map_line = luaL_checkstring (L, 2);
		description = lua_tostring (L, 3);
		map = rspamd_mempool_alloc0 (cfg->cfg_pool, sizeof (*map));
		map->data.radix = radix_create_compressed ();
		map->type = RSPAMD_LUA_MAP_RADIX;

		if ((m = rspamd_map_add (cfg, map_line, description,
				rspamd_radix_read,
				rspamd_radix_fin,
				(void **)&map->data.radix)) == NULL) {
			msg_warn_config ("invalid radix map %s", map_line);
			radix_destroy_compressed (map->data.radix);
			lua_pushnil (L);
			return 1;
		}

		map->map = m;
		pmap = lua_newuserdata (L, sizeof (void *));
		*pmap = map;
		rspamd_lua_setclass (L, "rspamd{map}", -1);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;

}

gint
lua_config_radix_from_config (lua_State *L)
{
	struct rspamd_config *cfg = lua_check_config (L, 1);
	const gchar *mname, *optname;
	const ucl_object_t *obj;
	struct rspamd_lua_map *map, **pmap;

	if (!cfg) {
		return luaL_error (L, "invalid arguments");
	}

	mname = luaL_checkstring (L, 2);
	optname = luaL_checkstring (L, 3);

	if (mname && optname) {
		obj = rspamd_config_get_module_opt (cfg, mname, optname);
		if (obj) {
			map = rspamd_mempool_alloc0 (cfg->cfg_pool, sizeof (*map));
			map->data.radix = radix_create_compressed ();
			map->type = RSPAMD_LUA_MAP_RADIX;
			map->data.radix = radix_create_compressed ();
			map->flags |= RSPAMD_LUA_MAP_FLAG_EMBEDDED;
			radix_add_generic_iplist (ucl_obj_tostring (obj), &map->data.radix);
			pmap = lua_newuserdata (L, sizeof (void *));
			*pmap = map;
			rspamd_lua_setclass (L, "rspamd{map}", -1);
		} else {
			msg_warn_config ("Couldnt find config option [%s][%s]", mname,
					optname);
			lua_pushnil (L);
		}

	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

gint
lua_config_add_hash_map (lua_State *L)
{
	struct rspamd_config *cfg = lua_check_config (L, 1);
	const gchar *map_line, *description;
	struct rspamd_lua_map *map, **pmap;
	struct rspamd_map *m;

	if (cfg) {
		map_line = luaL_checkstring (L, 2);
		description = lua_tostring (L, 3);
		map = rspamd_mempool_alloc0 (cfg->cfg_pool, sizeof (*map));
		map->data.hash = g_hash_table_new (rspamd_strcase_hash,
				rspamd_strcase_equal);
		map->type = RSPAMD_LUA_MAP_SET;

		if ((m = rspamd_map_add (cfg, map_line, description,
				rspamd_hosts_read,
				rspamd_hosts_fin,
				(void **)&map->data.hash)) == NULL) {
			msg_warn_config ("invalid set map %s", map_line);
			g_hash_table_destroy (map->data.hash);
			lua_pushnil (L);
			return 1;
		}

		map->map = m;
		pmap = lua_newuserdata (L, sizeof (void *));
		*pmap = map;
		rspamd_lua_setclass (L, "rspamd{map}", -1);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;

}

gint
lua_config_add_kv_map (lua_State *L)
{
	struct rspamd_config *cfg = lua_check_config (L, 1);
	const gchar *map_line, *description;
	struct rspamd_lua_map *map, **pmap;
	struct rspamd_map *m;

	if (cfg) {
		map_line = luaL_checkstring (L, 2);
		description = lua_tostring (L, 3);
		map = rspamd_mempool_alloc0 (cfg->cfg_pool, sizeof (*map));
		map->data.hash = g_hash_table_new (rspamd_strcase_hash,
				rspamd_strcase_equal);
		map->type = RSPAMD_LUA_MAP_HASH;

		if ((m = rspamd_map_add (cfg, map_line, description,
				rspamd_kv_list_read,
				rspamd_kv_list_fin,
				(void **)&map->data.hash)) == NULL) {
			msg_warn_config ("invalid hash map %s", map_line);
			g_hash_table_destroy (map->data.hash);
			lua_pushnil (L);
			return 1;
		}

		map->map = m;
		pmap = lua_newuserdata (L, sizeof (void *));
		*pmap = map;
		rspamd_lua_setclass (L, "rspamd{map}", -1);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}


static gchar *
lua_map_read (rspamd_mempool_t *pool, gchar *chunk, gint len,
	struct map_cb_data *data)
{
	struct lua_map_callback_data *cbdata, *old;

	if (data->cur_data == NULL) {
		cbdata = g_slice_alloc0 (sizeof (*cbdata));
		old = (struct lua_map_callback_data *)data->prev_data;
		cbdata->L = old->L;
		cbdata->ref = old->ref;
		cbdata->lua_map = old->lua_map;
		data->cur_data = cbdata;
	}
	else {
		cbdata = (struct lua_map_callback_data *)data->cur_data;
	}

	if (cbdata->data == NULL) {
		cbdata->data = g_string_new_len (chunk, len);
	}
	else {
		g_string_append_len (cbdata->data, chunk, len);
	}

	return NULL;
}

static void
lua_map_fin (rspamd_mempool_t * pool, struct map_cb_data *data)
{
	struct lua_map_callback_data *cbdata, *old;
	struct rspamd_lua_map **pmap;

	if (data->prev_data) {
		/* Cleanup old data */
		old = (struct lua_map_callback_data *)data->prev_data;
		if (old->data) {
			g_string_free (old->data, TRUE);
		}
		g_slice_free1 (sizeof (*old), old);
		data->prev_data = NULL;
	}

	if (data->cur_data) {
		cbdata = (struct lua_map_callback_data *)data->cur_data;
	}
	else {
		msg_err_pool ("no data read for map");
		return;
	}

	if (cbdata->ref == -1) {
		msg_err_pool ("map has no callback set");
	}
	else if (cbdata->data != NULL && cbdata->data->len != 0) {
		lua_rawgeti (cbdata->L, LUA_REGISTRYINDEX, cbdata->ref);
		lua_pushlstring (cbdata->L, cbdata->data->str, cbdata->data->len);
		pmap = lua_newuserdata (cbdata->L, sizeof (void *));
		*pmap = cbdata->lua_map;
		rspamd_lua_setclass (cbdata->L, "rspamd{map}", -1);

		if (lua_pcall (cbdata->L, 2, 0, 0) != 0) {
			msg_info_pool ("call to %s failed: %s", "local function",
				lua_tostring (cbdata->L, -1));
			lua_pop (cbdata->L, 1);
		}
	}
}

gint
lua_config_add_map (lua_State *L)
{
	struct rspamd_config *cfg = lua_check_config (L, 1);
	const gchar *map_line, *description;
	struct lua_map_callback_data *cbdata;
	struct rspamd_lua_map *map, **pmap;
	struct rspamd_map *m;
	int cbidx;

	if (cfg) {
		map_line = luaL_checkstring (L, 2);

		if (lua_gettop (L) == 4) {
			description = lua_tostring (L, 3);
			cbidx = 4;
		}
		else {
			description = NULL;
			cbidx = 3;
		}

		map = rspamd_mempool_alloc (cfg->cfg_pool, sizeof (*map));
		map->type = RSPAMD_LUA_MAP_CALLBACK;
		map->data.cbdata = rspamd_mempool_alloc (cfg->cfg_pool,
				sizeof (*map->data.cbdata));
		cbdata = map->data.cbdata;
		cbdata->L = L;
		cbdata->data = NULL;
		cbdata->lua_map = map;

		if (lua_type (L, cbidx) == LUA_TFUNCTION) {
			lua_pushvalue (L, cbidx);
			/* Get a reference */
			cbdata->ref = luaL_ref (L, LUA_REGISTRYINDEX);
		}
		else {
			/*
			 * Now we can create maps with delayed callbacks, to allow better
			 * closures generation
			 */
			cbdata->ref = -1;
		}

		if ((m = rspamd_map_add (cfg, map_line, description,
				lua_map_read, lua_map_fin,
				(void **)&map->data.cbdata)) == NULL) {
			msg_warn_config ("invalid map %s", map_line);
			lua_pushnil (L);
		}
		else {
			map->map = m;
			pmap = lua_newuserdata (L, sizeof (void *));
			*pmap = map;
			rspamd_lua_setclass (L, "rspamd{map}", -1);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

/* Radix and hash table functions */
static gint
lua_map_get_key (lua_State * L)
{
	struct rspamd_lua_map *map = lua_check_map (L, 1);
	radix_compressed_t *radix;
	struct rspamd_lua_ip *addr = NULL;
	const gchar *key, *value = NULL;
	gpointer ud;
	guint32 key_num = 0;
	gboolean ret = FALSE;

	if (map) {
		if (map->type == RSPAMD_LUA_MAP_RADIX) {
			radix = map->data.radix;

			if (lua_type (L, 2) == LUA_TNUMBER) {
				key_num = luaL_checknumber (L, 2);
				key_num = htonl (key_num);
			}
			else if (lua_type (L, 2) == LUA_TUSERDATA) {
				ud = luaL_checkudata (L, 2, "rspamd{ip}");
				if (ud != NULL) {
					addr = *((struct rspamd_lua_ip **)ud);

					if (addr->addr == NULL) {
						addr = NULL;
					}
				}
				else {
					msg_err ("invalid userdata type provided, rspamd{ip} expected");
				}
			}

			if (addr != NULL) {
				if (radix_find_compressed_addr (radix, addr->addr)
						!=  RADIX_NO_VALUE) {
					ret = TRUE;
				}
			}
			else if (key_num != 0) {
				if (radix_find_compressed (radix, (guint8 *)&key_num, sizeof (key_num))
						!= RADIX_NO_VALUE) {
					ret = TRUE;
				}
			}
		}
		else if (map->type == RSPAMD_LUA_MAP_SET) {
			key = lua_tostring (L, 2);

			if (key) {
				ret = g_hash_table_lookup (map->data.hash, key) != NULL;
			}
		}
		else {
			/* key-value map */
			key = lua_tostring (L, 2);

			if (key) {
				value = g_hash_table_lookup (map->data.hash, key);

				if (value) {
					lua_pushstring (L, value);
					return 1;
				}
			}
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	lua_pushboolean (L, ret);
	return 1;
}

static int
lua_map_is_signed (lua_State *L)
{
	struct rspamd_lua_map *map = lua_check_map (L, 1);
	gboolean ret = FALSE;

	if (map != NULL) {
		if (map->map) {
			if (map->map->is_signed) {
				ret = TRUE;
			}
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	lua_pushboolean (L, ret);
	return 1;
}

static int
lua_map_get_proto (lua_State *L)
{
	struct rspamd_lua_map *map = lua_check_map (L, 1);
	const gchar *ret = "undefined";

	if (map != NULL) {
		if ((map->flags & RSPAMD_LUA_MAP_FLAG_EMBEDDED) || map->map == NULL) {
			ret = "embedded";
		}
		else {
			switch (map->map->protocol) {
			case MAP_PROTO_FILE:
				ret = "file";
				break;
			case MAP_PROTO_HTTP:
				ret = "http";
				break;
			}
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	lua_pushstring (L, ret);
	return 1;
}

static int
lua_map_get_sign_key (lua_State *L)
{
	struct rspamd_lua_map *map = lua_check_map (L, 1);
	GString *ret = NULL;

	if (map != NULL) {
		if (map->flags & RSPAMD_LUA_MAP_FLAG_EMBEDDED) {
			lua_pushnil (L);

			return 1;
		}
		if (map->map && map->map->trusted_pubkey) {
			ret = rspamd_pubkey_print (map->map->trusted_pubkey,
					RSPAMD_KEYPAIR_PUBKEY|RSPAMD_KEYPAIR_BASE32);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	if (ret) {
		lua_pushlstring (L, ret->str, ret->len);
		g_string_free (ret, TRUE);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

static int
lua_map_set_sign_key (lua_State *L)
{
	struct rspamd_lua_map *map = lua_check_map (L, 1);
	const gchar *pk_str;
	struct rspamd_cryptobox_pubkey *pk;
	gsize len;

	pk_str = lua_tolstring (L, 2, &len);

	if (map && pk_str) {

		if ((map->flags & RSPAMD_LUA_MAP_FLAG_EMBEDDED) || !map->map) {
			return luaL_error (L, "cannot set key for embedded maps");
		}

		pk = rspamd_pubkey_from_base32 (pk_str, len, RSPAMD_KEYPAIR_SIGN,
				RSPAMD_CRYPTOBOX_MODE_25519);

		if (!pk) {
			return luaL_error (L, "invalid pubkey string");
		}

		if (map->map->trusted_pubkey) {
			/* Unref old pk */
			rspamd_pubkey_unref (map->map->trusted_pubkey);
		}

		map->map->trusted_pubkey = pk;
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 0;
}

static int
lua_map_set_callback (lua_State *L)
{
	struct rspamd_lua_map *map = lua_check_map (L, 1);

	if (!map || map->type != RSPAMD_LUA_MAP_CALLBACK || map->data.cbdata == NULL) {
		return luaL_error (L, "invalid map");
	}

	if (lua_type (L, 2) != LUA_TFUNCTION) {
		return luaL_error (L, "invalid callback");
	}

	lua_pushvalue (L, 2);
	/* Get a reference */
	map->data.cbdata->ref = luaL_ref (L, LUA_REGISTRYINDEX);

	return 0;
}

static int
lua_map_get_uri (lua_State *L)
{
	struct rspamd_lua_map *map = lua_check_map (L, 1);
	const gchar *ret = "undefined";

	if (map != NULL) {
		if ((map->flags & RSPAMD_LUA_MAP_FLAG_EMBEDDED) || map->map == NULL) {
			ret = "embedded";
		}
		else {
			ret = map->map->uri;
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	lua_pushstring (L, ret);
	return 1;
}

void
luaopen_map (lua_State * L)
{
	rspamd_lua_new_class (L, "rspamd{map}", maplib_m);

	lua_pop (L, 1);
}
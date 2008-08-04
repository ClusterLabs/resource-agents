int read_cman_nodes(struct objdb_iface_ver0 *objdb, unsigned int *config_version, int check_nodeids);
int read_cman_config(struct objdb_iface_ver0 *objdb, unsigned int *config_version);

/* These just make the access a little neater */
static inline int objdb_get_string(struct objdb_iface_ver0 *objdb, unsigned int object_service_handle,
				   char *key, char **value)
{
	int res;

	*value = NULL;
	if ( !(res = objdb->object_key_get(object_service_handle,
					   key,
					   strlen(key),
					   (void *)value,
					   NULL))) {
		if (*value)
			return 0;
	}
	return -1;
}

static inline void objdb_get_int(struct objdb_iface_ver0 *objdb, unsigned int object_service_handle,
				 char *key, unsigned int *intvalue, unsigned int default_value)
{
	char *value = NULL;

	*intvalue = default_value;

	if (!objdb->object_key_get(object_service_handle, key, strlen(key),
				   (void *)&value, NULL)) {
		if (value) {
			*intvalue = atoi(value);
		}
	}
}

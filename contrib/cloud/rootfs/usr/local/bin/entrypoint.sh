#!/bin/bash
set -e

init_db ()
{
    if [[ -f "$CUBRID_DATABASES/databases.txt" ]]; then
        if grep -qwe "^$DB_NAME" "$CUBRID_DATABASES/databases.txt"; then
            echo "Database '$DB_NAME' is initialized already"
            return 0
        fi
    else
        touch "$CUBRID_DATABASES/databases.txt"
    fi

    echo "Initializing database '$DB_NAME'"

    if [[ ! -d "$CUBRID_DATABASES/$DB_NAME" ]]; then
        mkdir -p "$CUBRID_DATABASES/$DB_NAME"
    fi

    if [[ "$CUBRID_DB_HOST" ]]; then
        CUBRID_SERVER_NAME=${CUBRID_DB_HOST}
    else
        CUBRID_SERVER_NAME=$(hostname -f)
    fi

    cd "$CUBRID_DATABASES/$DB_NAME" \
        && cubrid createdb --db-volume-size=${DB_VOLUME_SIZE} --server-name=${CUBRID_SERVER_NAME} ${DB_NAME} ${DB_LOCALE}

    if [[ "$CUBRID_USER" && "$CUBRID_USER" != "dba" && "$CUBRID_USER" != "public" ]]; then
        csql -u dba -S ${DB_NAME} -c "CREATE USER $CUBRID_USER PASSWORD '$CUBRID_PASSWORD';"
    fi
}

init_ha ()
{
    # turn on HA mode
    if grep -we 'ha_mode[ ]*=[ ]*on' ; then
        echo "HA mode is on already"
    else
        echo "ha_mode=on" >> ${CUBRID}/conf/cubrid.conf
    fi
    # config for HA
    echo "[common]" >> ${CUBRID}/conf/cubrid_ha.conf
    echo "ha_port_id=59901" >> ${CUBRID}/conf/cubrid_ha.conf
    echo "ha_node_list=cubrid@$CUBRID_DB_HOST" >> ${CUBRID}/conf/cubrid_ha.conf
    echo "ha_db_list=$DB_NAME" >> ${CUBRID}/conf/cubrid_ha.conf
    echo "ha_copy_sync_mode=sync:sync" >> ${CUBRID}/conf/cubrid_ha.conf
    echo "ha_apply_max_mem_size=300" >> ${CUBRID}/conf/cubrid_ha.conf
    echo "ha_copy_log_max_archives=1" >> ${CUBRID}/conf/cubrid_ha.conf
}

if [[ $# -eq 0 ]]; then
    case "$CUBRID_COMPONENTS" in
        BROKER)
            cubrid broker start
            if [[ "$CUBRID_DB_HOST" ]]; then
                if [[ -f "$CUBRID_DATABASES/databases.txt" ]]; then
                    if grep -qwe "^$DB_NAME" "$CUBRID_DATABASES/databases.txt"; then
                        echo "Database '$DB_NAME' exists already"
                    fi
                else
                    echo "$DB_NAME / $CUBRID_DB_HOST / file:/" > ${CUBRID_DATABASES}/databases.txt
                fi
            fi
            ;;
        SERVER)
            init_db && cubrid server start $DB_NAME
            ;;
        MASTER|SLAVE)
            init_db && init_ha && cubrid heartbeat start
            ;;
        HA)
            init_db && init_ha && cubrid heartbeat start && cubrid broker start
            ;;
        ALL)
            init_db && cubrid server start $DB_NAME && cubrid broker start
            ;;
        *)
            echo "Unknown CUBRID_COMPONENTS '$CUBRID_COMPONENTS'" && false
            ;;
    esac

    cubrid_rel

    exec /usr/bin/tail -F /dev/null
else
    exec "$@"
fi

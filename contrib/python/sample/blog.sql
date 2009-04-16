create table posts (
        id integer,
        title varchar(255),
        body string,
        last_updated timestamp
        );

create table comments (
        id integer,
        body string,
        create_at timestamp,
        post_id integer
        );

create serial ser_post_id start with 1;

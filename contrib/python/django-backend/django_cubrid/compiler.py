from django.db.models.sql import compiler

class SQLCompiler(compiler.SQLCompiler):

    def as_sql(self, with_limits=True, with_col_aliases=False):
        """
        Creates the SQL for this query. Returns the SQL string and list of parameters.
        If 'with_limits' is False, any limit/offset information is not included in the query.
        """

        if with_limits and self.query.low_mark == self.query.high_mark:
            return '', ()

        self.pre_sql_setup()
        out_cols = self.get_columns(with_col_aliases)
        ordering, ordering_group_by = self.get_ordering()

        # This must come after 'select' and 'ordering' -- see docstring of
        # get_from_clause() for details.
        from_, f_params = self.get_from_clause()

        qn = self.quote_name_unless_alias

        where, w_params = self.query.where.as_sql(qn=qn, connection=self.connection)
        having, h_params = self.query.having.as_sql(qn=qn, connection=self.connection)
        params = []
        for val in self.query.extra_select.itervalues():
            params.extend(val[1])

        result = ['SELECT']
        if self.query.distinct:
            result.append('DISTINCT')
        result.append(', '.join(out_cols + self.query.ordering_aliases))

        result.append('FROM')
        result.extend(from_)
        params.extend(f_params)

        if where:
            result.append('WHERE %s' % where)
            params.extend(w_params)

        grouping, gb_params = self.get_grouping()
        if grouping:
            if ordering:
                # If the backend can't group by PK (i.e., any database
                # other than MySQL), then any fields mentioned in the
                # ordering clause needs to be in the group by clause.
                if not self.connection.features.allows_group_by_pk:
                    for col, col_params in ordering_group_by:
                        if col not in grouping:
                            grouping.append(str(col))
                            gb_params.extend(col_params)
            else:
                ordering = self.connection.ops.force_no_ordering()
            result.append('GROUP BY %s' % ', '.join(grouping))
            params.extend(gb_params)

        if having:
            result.append('HAVING %s' % having)
            params.extend(h_params)

        if ordering:
            result.append('ORDER BY %s' % ', '.join(ordering))

        if with_limits:
            if self.query.high_mark is not None:
                start_mark = self.query.high_mark - self.query.low_mark
                if self.query.low_mark:
                    result.append('LIMIT %d,%d' % (self.query.low_mark, start_mark))
                else:
                    result.append('LIMIT %d' % start_mark)
            else:
                val = self.connection.ops.no_limit_value()
                if val:
                    if self.query.low_mark:
                        result.append('LIMIT %d,%d' % (self.query.low_mark, val))
                    else:
                        result.append('LIMIT %d' % val)

        return ' '.join(result), tuple(params)

class SQLInsertCompiler(compiler.SQLInsertCompiler, SQLCompiler):
    pass

class SQLDeleteCompiler(compiler.SQLDeleteCompiler, SQLCompiler):
    pass

class SQLUpdateCompiler(compiler.SQLUpdateCompiler, SQLCompiler):
    pass

class SQLAggregateCompiler(compiler.SQLAggregateCompiler, SQLCompiler):
    pass

class SQLDateCompiler(compiler.SQLDateCompiler, SQLCompiler):
    pass

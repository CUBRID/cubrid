#include "object_print_parser.hpp"
#include "class_object.h"
#include "dbdef.h"
#include "dbi.h"
#include "dbtype.h"
#include "dbval.h"
#include "misc_string.h"
#include "object_print_common.hpp"
#include "object_domain.h"
#include "object_print_class_description.hpp"
#include "object_print_util.hpp"
#include "parse_tree.h"
#include "schema_manager.h"
#include "set_object.h"
#include "string_buffer.hpp"
#include "trigger_manager.h"
#include "work_space.h"
#include <assert.h>

//--------------------------------------------------------------------------------
void object_print_parser::describe_comment(const char *comment){
    db_value comment_value;
    assert(comment != NULL);
    DB_MAKE_NULL(&comment_value);
    DB_MAKE_STRING(&comment_value, comment);
    m_buf("COMMENT ");
    if(comment != NULL && comment[0] != '\0')
    {
        object_print_common obj_print(m_buf);
        obj_print.describe_value(&comment_value);
    }
    else
    {
        m_buf("''");
    }
    pr_clear_value(&comment_value);
}

//--------------------------------------------------------------------------------
void object_print_parser::describe_partition_parts(const sm_partition& parts, object_print::type prt_type) {
    DB_VALUE ele;
    int setsize, i;
    object_print_common obj_print(m_buf);

    DB_MAKE_NULL(&ele);

    m_buf("PARTITION ");
    describe_identifier(parts.pname, prt_type);

    switch(parts.partition_type)
    {
    case PT_PARTITION_HASH:
        break;
    case PT_PARTITION_RANGE:
        m_buf(" VALUES LESS THAN ");
        if(!set_get_element(parts.values, 1, &ele))
        {			/* 0:MIN, 1: MAX */
            if(DB_IS_NULL(&ele))
            {
                m_buf("MAXVALUE");
            }
            else
            {
                m_buf("(");
                obj_print.describe_value(&ele);
                m_buf(")");
            }
        }
        break;
    case PT_PARTITION_LIST:
        m_buf(" VALUES IN (");
        setsize = set_size(parts.values);
        for(i = 0; i < setsize; i++)
        {
            if(i > 0)
            {
                m_buf(", ");
            }
            if(set_get_element(parts.values, i, &ele) == NO_ERROR)
            {
                obj_print.describe_value(&ele);
            }
        }
        m_buf(")");
        break;
    }
    if(parts.comment != NULL && parts.comment[0] != '\0')
    {
        m_buf(" ");
        describe_comment(parts.comment);
    }
    pr_clear_value(&ele);
}

/*
 * object_print_identifier() - help function to print identifier string.
 *                             if prt_type is OBJ_PRINT_SHOW_CREATE_TABLE,
 *                             we need wrap it with "[" and "]".
 *      return: advanced buffer pointer
 *  parser(in) :
 *  buffer(in) : current buffer pointer
 *  identifier(in) : identifier string,.such as: table name.
 *  prt_type(in): the print type: csql schema or show create table
 *
 */
 //--------------------------------------------------------------------------------
void object_print_parser::describe_identifier(const char* identifier, object_print::type prt_type) {
    if(prt_type == object_print::CSQL_SCHEMA_COMMAND)
    {
        m_buf("%s", identifier);
    }
    else
    {//prt_type == OBJ_PRINT_SHOW_CREATE_TABLE
        m_buf("[%s]", identifier);
    }
}

/* CLASS COMPONENT DESCRIPTION FUNCTIONS */

/*
 * obj_print_describe_domain() - Describe the domain of an attribute
 *      return: advanced buffer pointer
 *  parser(in) :
 *  buffer(in) : current buffer pointer
 *  domain(in) : domain structure to describe
 *  prt_type(in): the print type: csql schema or show create table
 *  force_print_collation(in): true if collation is printed no matter system
 *			       collation
 *
 */
 //--------------------------------------------------------------------------------
void object_print_parser::describe_domain(/*const*/tp_domain& domain, object_print::type prt_type, bool force_print_collation) {
    TP_DOMAIN *temp_domain;
    char temp_buffer[27];
    int precision = 0, idx, count;
    int has_collation;

    /* filter first, usually not necessary but this is visible */
    sm_filter_domain(&domain, NULL);

    for(temp_domain = &domain; temp_domain != NULL; temp_domain = temp_domain->next)
    {
        has_collation = 0;
        switch(TP_DOMAIN_TYPE(temp_domain))
        {
        case DB_TYPE_INTEGER:
        case DB_TYPE_BIGINT:
        case DB_TYPE_FLOAT:
        case DB_TYPE_DOUBLE:
        case DB_TYPE_BLOB:
        case DB_TYPE_CLOB:
        case DB_TYPE_TIME:
        case DB_TYPE_TIMETZ:
        case DB_TYPE_TIMELTZ:
        case DB_TYPE_UTIME:
        case DB_TYPE_TIMESTAMPTZ:
        case DB_TYPE_TIMESTAMPLTZ:
        case DB_TYPE_DATETIME:
        case DB_TYPE_DATETIMETZ:
        case DB_TYPE_DATETIMELTZ:
        case DB_TYPE_DATE:
        case DB_TYPE_MONETARY:
        case DB_TYPE_SUB:
        case DB_TYPE_POINTER:
        case DB_TYPE_ERROR:
        case DB_TYPE_SHORT:
        case DB_TYPE_VOBJ:
        case DB_TYPE_OID:
        case DB_TYPE_NULL:
        case DB_TYPE_VARIABLE:
        case DB_TYPE_DB_VALUE:
            strcpy(temp_buffer, temp_domain->type->name);
            m_buf("%s", ustr_upper(temp_buffer));
            break;

        case DB_TYPE_OBJECT:
            if(temp_domain->class_mop != NULL)
            {
                describe_identifier(sm_get_ch_name(temp_domain->class_mop), prt_type);
            }
            else
            {
                m_buf("%s", temp_domain->type->name);
            }
            break;

        case DB_TYPE_VARCHAR:
            has_collation = 1;
            if(temp_domain->precision == TP_FLOATING_PRECISION_VALUE)
            {
                m_buf("STRING");
                break;
            }
            /* fall through */
        case DB_TYPE_CHAR:
        case DB_TYPE_NCHAR:
        case DB_TYPE_VARNCHAR:
            has_collation = 1;
        case DB_TYPE_BIT:
        case DB_TYPE_VARBIT:
            strcpy(temp_buffer, temp_domain->type->name);
            if(temp_domain->precision == TP_FLOATING_PRECISION_VALUE)
            {
                precision = DB_MAX_STRING_LENGTH;
            }
            else
            {
                precision = temp_domain->precision;
            }
            m_buf("%s(%d)", ustr_upper(temp_buffer), precision);
            break;

        case DB_TYPE_NUMERIC:
            strcpy(temp_buffer, temp_domain->type->name);
            m_buf("%s(%d,%d)", ustr_upper(temp_buffer), temp_domain->precision, temp_domain->scale);
            break;

        case DB_TYPE_SET:
        case DB_TYPE_MULTISET:
        case DB_TYPE_SEQUENCE:
            strcpy(temp_buffer, temp_domain->type->name);
            ustr_upper(temp_buffer);
            m_buf("%s OF ", temp_buffer);
            if(temp_domain->setdomain != NULL)
            {
                if(temp_domain->setdomain->next != NULL && prt_type == object_print::SHOW_CREATE_TABLE)
                {
                    m_buf("(");
                    describe_domain(*temp_domain->setdomain, prt_type, force_print_collation);
                    m_buf(")");
                }
                else
                {
                    describe_domain(*temp_domain->setdomain, prt_type, force_print_collation);
                }
            }
            break;
        case DB_TYPE_ENUMERATION:
            has_collation = 1;
            strcpy(temp_buffer, temp_domain->type->name);
            m_buf("%s(", ustr_upper(temp_buffer));
            count = DOM_GET_ENUM_ELEMS_COUNT(temp_domain);
            for(idx = 1; idx <= count; idx++)
            {
                if(idx > 1)
                {
                    m_buf(", ");
                }
                m_buf("'");
                m_buf(DB_GET_ENUM_ELEM_STRING_SIZE(&DOM_GET_ENUM_ELEM(temp_domain, idx)), DB_GET_ENUM_ELEM_STRING(&DOM_GET_ENUM_ELEM(temp_domain, idx)));
                m_buf("'");
            }
            m_buf(")");
            break;
        default:
            break;
        }

        if(has_collation && (force_print_collation || temp_domain->collation_id != LANG_SYS_COLLATION))
        {
            m_buf(" COLLATE %s", lang_get_collation_name(temp_domain->collation_id));
        }
        if(temp_domain->next != NULL)
        {
            m_buf(", ");
        }
    }
}

/*
 * obj_print_describe_argument() - Describes a method argument
 *      return: advanced buffer pointer
 *  parser(in) :
 *  buffer(in) : current buffer pointer
 *  argument_p(in) : method argument to describe
 *  prt_type(in): the print type: csql schema or show create table
 *
 */
 //--------------------------------------------------------------------------------
void object_print_parser::describe_argument(const sm_method_argument& argument, object_print::type prt_type)
{
  if(argument.domain != NULL)
  {
      /* method and its arguments do not inherit collation from class, collation printing is not enforced */
      describe_domain(*argument.domain, prt_type, false);
  }
  else if(argument.type)
  {
      m_buf("%s", argument.type->name);
  }
  else
  {
      m_buf("invalid type");
  }
}

/*
 * describe_method() - Describes the definition of a method in a class
 *      return: advanced buffer pointer
 *  parser(in) : current buffer pointer
 *  op(in) : class with method
 *  method_p(in) : method to describe
 *  prt_type(in): the print type: csql schema or show create table
 *
 */
 //--------------------------------------------------------------------------------
void object_print_parser::describe_method(const struct db_object& op, const sm_method& method, object_print::type prt_type) {
    SM_METHOD_SIGNATURE *signature_p;

    /* assume for the moment that there can only be one signature, simplifies the output */
    describe_identifier(method.header.name, prt_type);
    signature_p = method.signatures;
    if(signature_p == NULL)
    {
        m_buf("()");
    }
    else
    {
        m_buf("(");
        describe_signature(*signature_p, prt_type);
        m_buf(") ");

        if(signature_p->value != NULL)
        {
            /* make this look more like the actual definition instead strcpy(line, "returns "); line += strlen(line); */
            describe_argument(*signature_p->value, prt_type);
        }
        if(signature_p->function_name != NULL)
        {
            m_buf(" FUNCTION ");
            describe_identifier(signature_p->function_name, prt_type);
        }
    }
    /* add the inheritance source */
    if(method.class_mop != NULL && method.class_mop != &op)
    {

        m_buf("(from ");
        describe_identifier(sm_get_ch_name(method.class_mop), prt_type);
        m_buf(")");
    }
}

/*
 * obj_print_describe_signature() - Describes a method signature
 *      return: advanced buffer pointer
 *  parser(in) :
 *  buffer(in) : current buffer pointer
 *  signature_p(in) : signature to describe
 *  prt_type(in): the print type: csql schema or show create table
 *
 */
 //--------------------------------------------------------------------------------
void object_print_parser::describe_signature(const sm_method_signature& signature, object_print::type prt_type) {
    SM_METHOD_ARGUMENT *argument_p;
    int i;

    for(i = 1; i <= signature.num_args; i++)
    {
        for(argument_p = signature.args; argument_p!=NULL && argument_p->index!=i; argument_p=argument_p->next);
        if(argument_p != NULL)
        {
            describe_argument(*argument_p, prt_type);
        }
        else
        {
            m_buf("??");
        }
        if(i < signature.num_args)
        {
            m_buf(", ");
        }
    }
}

/* CLASS COMPONENT DESCRIPTION FUNCTIONS */

/*
 * obj_print_describe_attribute() - Describes the definition of an attribute
 *                                  in a class
 *      return: advanced bufbuffer pointer
 *  class_p(in) : class being examined
 *  parser(in) :
 *  attribute_p(in) : attribute of the class
 *  is_inherited(in) : is the attribute inherited
 *  prt_type(in): the print type: csql schema or show create table
 *  obj_print_describe_attribute(in):
 *
 */
 //--------------------------------------------------------------------------------
void object_print_parser::describe_attribute(const struct db_object& cls, const sm_attribute& attribute, bool is_inherited, object_print::type prt_type, bool force_print_collation) {
    char str_buf[NUMERIC_MAX_STRING_SIZE];
    object_print_common obj_print(m_buf);

    if(prt_type == object_print::CSQL_SCHEMA_COMMAND)
    {
        m_buf("%-20s ", attribute.header.name);
    }
    else
    {/* prt_type == OBJ_PRINT_SHOW_CREATE_TABLE */
        describe_identifier(attribute.header.name, prt_type);
        m_buf(" ");
    }

    /* could filter here but do in describe_domain */
    describe_domain(*attribute.domain, prt_type, force_print_collation);

    if(attribute.header.name_space == ID_SHARED_ATTRIBUTE)
    {
        m_buf(" SHARED ");
        if(!DB_IS_NULL(&attribute.default_value.value))
        {
            obj_print.describe_value(&attribute.default_value.value);
        }
    }
    else if(attribute.header.name_space == ID_ATTRIBUTE)
    {
        if(attribute.flags & SM_ATTFLAG_AUTO_INCREMENT)
        {
            m_buf(" AUTO_INCREMENT ");
            assert(is_inherited || attribute.auto_increment != NULL);
            if(prt_type == object_print::SHOW_CREATE_TABLE)
            {
                DB_VALUE min_val, inc_val;
                char buff[DB_MAX_NUMERIC_PRECISION * 2 + 4];
                int offset;
                assert(attribute.auto_increment != NULL);

                DB_MAKE_NULL(&min_val);
                DB_MAKE_NULL(&inc_val);

                if(db_get(attribute.auto_increment, "min_val", &min_val) != NO_ERROR)
                {
                    return;
                }

                if(db_get(attribute.auto_increment, "increment_val", &inc_val) != NO_ERROR)
                {
                    pr_clear_value(&min_val);
                    return;
                }

                offset =
                    snprintf(buff, DB_MAX_NUMERIC_PRECISION + 3, "(%s, ", numeric_db_value_print(&min_val, str_buf));
                snprintf(buff + offset, DB_MAX_NUMERIC_PRECISION + 1, "%s)", numeric_db_value_print(&inc_val, str_buf));
                m_buf(buff);

                pr_clear_value(&min_val);
                pr_clear_value(&inc_val);
            }
        }
        if(!DB_IS_NULL(&attribute.default_value.value)
            || attribute.default_value.default_expr.default_expr_type != DB_DEFAULT_NONE)
        {
            const char *default_expr_type_str;

            m_buf(" DEFAULT ");

            if(attribute.default_value.default_expr.default_expr_op == T_TO_CHAR)
            {
                m_buf("TO_CHAR(");
            }

            default_expr_type_str = db_default_expression_string(attribute.default_value.default_expr.default_expr_type);
            if(default_expr_type_str != NULL)
            {
                m_buf("%s", default_expr_type_str);
            }
            else
            {
                assert(attribute.default_value.default_expr.default_expr_op == NULL_DEFAULT_EXPRESSION_OPERATOR);
                obj_print.describe_value(&attribute.default_value.value);
            }

            if(attribute.default_value.default_expr.default_expr_op == T_TO_CHAR)
            {
                if(attribute.default_value.default_expr.default_expr_format)
                {
                    m_buf(", \'%s\'", attribute.default_value.default_expr.default_expr_format);
                }
                m_buf(")");
            }
        }
    }
    else if(attribute.header.name_space == ID_CLASS_ATTRIBUTE)
    {
        if(!DB_IS_NULL(&attribute.default_value.value))
        {
            m_buf(" VALUE ");
            obj_print.describe_value(&attribute.default_value.value);
        }
    }

    if(attribute.flags & SM_ATTFLAG_NON_NULL)
    {
        m_buf(" NOT NULL");
    }
    if(attribute.class_mop != NULL && attribute.class_mop != &cls)
    {
        m_buf(" /* from ");
        describe_identifier(sm_get_ch_name(attribute.class_mop), prt_type);
        m_buf(" */");
    }

    if(attribute.comment != NULL && attribute.comment[0] != '\0')
    {
        m_buf(" ");
        describe_comment(attribute.comment);
    }
}

/*
 * obj_print_describe_constraint() - Describes the definition of an attribute
 *                                   in a class
 *      return: advanced buffer pointer
 *  parser(in) :
 *  class_p(in) : class being examined
 *  constraint_p(in) :
 *  prt_type(in): the print type: csql schema or show create table
 *
 */
 //--------------------------------------------------------------------------------
void object_print_parser::describe_constraint(const sm_class& cls, const sm_class_constraint& constraint, object_print::type prt_type) {
    SM_ATTRIBUTE **attribute_p;
    const int *asc_desc;
    const int *prefix_length;
    int k, n_attrs = 0;

    if(prt_type == object_print::CSQL_SCHEMA_COMMAND)
    {
        switch(constraint.type)
        {
        case SM_CONSTRAINT_INDEX:
            m_buf("INDEX ");
            break;
        case SM_CONSTRAINT_UNIQUE:
            m_buf("UNIQUE ");
            break;
        case SM_CONSTRAINT_REVERSE_INDEX:
            m_buf("REVERSE INDEX ");
            break;
        case SM_CONSTRAINT_REVERSE_UNIQUE:
            m_buf("REVERSE UNIQUE ");
            break;
        case SM_CONSTRAINT_PRIMARY_KEY:
            m_buf("PRIMARY KEY ");
            break;
        case SM_CONSTRAINT_FOREIGN_KEY:
            m_buf("FOREIGN KEY ");
            break;
        default:
            m_buf("CONSTRAINT ");
            break;
        }
        m_buf("%s ON %s (", constraint.name, sm_ch_name((MOBJ)&cls));
        asc_desc = NULL;		/* init */
        if(!SM_IS_CONSTRAINT_REVERSE_INDEX_FAMILY(constraint.type))
        {
            asc_desc = constraint.asc_desc;
        }
    }
    else
    {				/* prt_type == OBJ_PRINT_SHOW_CREATE_TABLE */
        switch(constraint.type)
        {
        case SM_CONSTRAINT_INDEX:
        case SM_CONSTRAINT_REVERSE_INDEX:
            m_buf(" INDEX ");
            describe_identifier(constraint.name, prt_type);
            break;
        case SM_CONSTRAINT_UNIQUE:
        case SM_CONSTRAINT_REVERSE_UNIQUE:
            m_buf(" CONSTRAINT ");
            describe_identifier(constraint.name, prt_type);
            m_buf(" UNIQUE KEY ");
            break;
        case SM_CONSTRAINT_PRIMARY_KEY:
            m_buf(" CONSTRAINT ");
            describe_identifier(constraint.name, prt_type);
            m_buf(" PRIMARY KEY ");
            break;
        case SM_CONSTRAINT_FOREIGN_KEY:
            m_buf(" CONSTRAINT ");
            describe_identifier(constraint.name, prt_type);
            m_buf(" FOREIGN KEY ");
            break;
        default:
            assert(false);
            break;
        }
        m_buf(" (");
        asc_desc = constraint.asc_desc;
    }

    prefix_length = constraint.attrs_prefix_length;

    /* If the index is a function index, then the corresponding expression is printed at the right position in the
     * attribute list. Since the expression is not part of the attribute list, when searching through that list we must
     * check if the position of the expression is reached and then print it. */
    k = 0;
    if(constraint.func_index_info)
    {
        n_attrs = constraint.func_index_info->attr_index_start + 1;
    }
    else
    {
        for(attribute_p = constraint.attributes; *attribute_p; attribute_p++)
        {
            n_attrs++;
        }
    }
    for(attribute_p = constraint.attributes; k < n_attrs; attribute_p++)
    {
        if(constraint.func_index_info && k == constraint.func_index_info->col_id)
        {
            if(k > 0)
            {
                m_buf(", ");
            }
            m_buf("%s", constraint.func_index_info->expr_str);
            if(constraint.func_index_info->fi_domain->is_desc)
            {
                m_buf(" DESC");
            }
            k++;
        }
        if(k == n_attrs)
        {
            break;
        }
        if(k > 0)
        {
            m_buf(", ");
        }
        describe_identifier((*attribute_p)->header.name, prt_type);

        if(prefix_length)
        {
            if(*prefix_length != -1)
            {
                m_buf("(%d)", *prefix_length);
            }

            prefix_length++;
        }

        if(asc_desc)
        {
            if(*asc_desc == 1)
            {
                m_buf(" DESC");
            }
            asc_desc++;
        }
        k++;
    }
    m_buf(")");

    if(constraint.filter_predicate && constraint.filter_predicate->pred_string)
    {
        m_buf(" WHERE %s", constraint.filter_predicate->pred_string);
    }

    if(constraint.type == SM_CONSTRAINT_FOREIGN_KEY && constraint.fk_info)
    {
        MOP ref_clsop;
        SM_CLASS *ref_cls;
        SM_CLASS_CONSTRAINT *c;

        ref_clsop = ws_mop(&(constraint.fk_info->ref_class_oid), NULL);
        if(au_fetch_class_force(ref_clsop, &ref_cls, AU_FETCH_READ) != NO_ERROR)
        {
            return;
        }

        m_buf(" REFERENCES ");
        describe_identifier(sm_ch_name((MOBJ)ref_cls), prt_type);

        if(prt_type == object_print::SHOW_CREATE_TABLE)
        {
            for(c = ref_cls->constraints; c; c = c->next)
            {
                if(c->type == SM_CONSTRAINT_PRIMARY_KEY && c->attributes != NULL)
                {
                    m_buf(" (");
                    for(k = 0; k < n_attrs; k++)
                    {
                        if(c->attributes[k] != NULL)
                        {
                            describe_identifier(c->attributes[k]->header.name, prt_type);
                            if(k != (n_attrs - 1))
                            {
                                m_buf(", ");
                            }
                        }
                    }
                    m_buf(")");
                    break;
                }
            }
        }

        m_buf(" ON DELETE %s", classobj_describe_foreign_key_action(constraint.fk_info->delete_action));

        if(prt_type == object_print::CSQL_SCHEMA_COMMAND)
        {
            m_buf(",");
        }
        m_buf(" ON UPDATE %s", classobj_describe_foreign_key_action(constraint.fk_info->update_action));
    }

    if(constraint.comment != NULL && constraint.comment[0] != '\0')
    {
        m_buf(" ");
        describe_comment(constraint.comment);
    }
}

/*
 * obj_print_describe_resolution() - Describes a resolution specifier
 *      return: advanced buffer pointer
 *  parser(in) :
 *  resolution_p(in) : resolution to describe
 *  prt_type(in): the print type: csql schema or show create table
 *
 */
 //--------------------------------------------------------------------------------
void object_print_parser::describe_resolution(const sm_resolution& resolution, object_print::type prt_type) {
  if(prt_type != object_print::SHOW_CREATE_TABLE)
  {
      if(resolution.name_space == ID_CLASS)
      {
          m_buf("inherit CLASS ");
      }
      else
      {
          m_buf("inherit ");
      }
  }
  describe_identifier(resolution.name, prt_type);
  m_buf(" of ");
  describe_identifier(sm_get_ch_name(resolution.class_mop), prt_type);
  if(resolution.alias != NULL)
  {
      m_buf(" as ");
      describe_identifier(resolution.alias, prt_type);
  }
}

/*
 * obj_print_describe_method_file () - Describes a method file.
 *   return: advanced buffer pointer
 *   parser(in) :
 *   class_p(in) :
 *   file_p(in): method file descriptor
 */
 //--------------------------------------------------------------------------------
void object_print_parser::describe_method_file(const struct db_object& obj, const sm_method_file& file) {
    m_buf("%s", file.name);
    if(file.class_mop != NULL && file.class_mop != &obj)
    {
        m_buf(" (from %s)", sm_get_ch_name(file.class_mop));
    }
}

/*
 * describe_class_trigger () - Describes the given trigger object to a buffer.
 *   return: PARSER_VARCHAR *
 *   parser(in): buffer for description
 *   trigger(in): trigger object
 * Note :
 *    This description is for the class help and as such it contains
 *    a condensed versino of the trigger help.
 */
 //--------------------------------------------------------------------------------
void object_print_parser::describe_class_trigger(const tr_trigger& trigger) {
    m_buf("%s : %s %s ", trigger.name, describe_trigger_condition_time(trigger), tr_event_as_string(trigger.event));
    if(trigger.attribute != NULL)
    {
        m_buf("OF %s", trigger.attribute);
    }
    if(trigger.comment != NULL && trigger.comment[0] != '\0')
    {
        m_buf(" ");
        describe_comment(trigger.comment);
    }
}

/*
 * obj_print_trigger_condition_time() -
 *      return: char *
 *  trigger(in) :
 */
 //--------------------------------------------------------------------------------
const char* object_print_parser::describe_trigger_condition_time(const tr_trigger& trigger) {
    DB_TRIGGER_TIME time = TR_TIME_NULL;
    if(trigger.condition != NULL)
    {
        time = trigger.condition->time;
    }
    else if(trigger.action != NULL)
    {
        time = trigger.action->time;
    }
    return (tr_time_as_string(time));
}

/*
 * obj_print_trigger_action_time() -
 *      return: char *
 *  trigger(in) :
 */
 //--------------------------------------------------------------------------------
const char* object_print_parser::describe_trigger_action_time(const tr_trigger& trigger) {
    DB_TRIGGER_TIME time = TR_TIME_NULL;
    if(trigger.action != NULL)
    {
        time = trigger.action->time;
    }
    return (tr_time_as_string(time));
}

/*
 * obj_print_describe_class() - Describes the definition of a class
 *   return: create table string
 *   parser(in):
 *   class_schema(in):
 *   class_op(in):
 */
//--------------------------------------------------------------------------------
void object_print_parser::describe_class(object_print::class_description& class_schema, struct db_object* class_op){
  char **line_ptr;
  /* class name */
  m_buf("CREATE TABLE %s", class_schema.name);

  /* under or as subclass of */
  if (class_schema.supers != NULL)
    {
      m_buf(" UNDER ");
      for (line_ptr = class_schema.supers; *line_ptr != NULL; line_ptr++)
      {
          if(line_ptr != class_schema.supers)
          {
              m_buf(", ");
          }
          m_buf("%s", (*line_ptr));
      }
    }

  /* class attributes */
  if (class_schema.class_attributes != NULL)
    {
      m_buf(" CLASS ATTRIBUTE (");
      for (line_ptr = class_schema.class_attributes; *line_ptr != NULL; line_ptr++)
      {
          if(line_ptr != class_schema.class_attributes)
          {
              m_buf(", ");
          }
          m_buf("%s", *line_ptr);
      }
      m_buf(")");
    }

  /* attributes and constraints */
  if (class_schema.attributes != NULL || class_schema.constraints != NULL)
    {
      m_buf(" (");
      if (class_schema.attributes != NULL)
      {
          for(line_ptr = class_schema.attributes; *line_ptr != NULL; line_ptr++)
          {
              if(line_ptr != class_schema.attributes)
              {
                  m_buf(", ");
              }
              m_buf("%s", *line_ptr);
          }
      }
      if (class_schema.constraints != NULL)
      {
          for(line_ptr = class_schema.constraints; *line_ptr != NULL; line_ptr++)
          {
              if(line_ptr != class_schema.constraints || class_schema.attributes != NULL)
              {
                  m_buf(", ");
              }
              m_buf("%s", *line_ptr);
          }
      }
      m_buf(")");
    }

  /* reuse_oid flag */
  if (sm_is_reuse_oid_class (class_op))
    {
      m_buf(" REUSE_OID");
      if (class_schema.collation != NULL)
      {
          m_buf(",");
      }
      else
      {
          m_buf(" ");
      }
  }

  /* collation */
  if (class_schema.collation != NULL)
    {
      m_buf(" COLLATE %s", class_schema.collation);
    }

  /* methods and class_methods */
  if (class_schema.methods != NULL || class_schema.class_methods != NULL)
    {
      m_buf(" METHOD ");
      if (class_schema.methods != NULL)
	{
	  for (line_ptr = class_schema.methods; *line_ptr != NULL; line_ptr++)
	    {
	      if (line_ptr != class_schema.methods)
		{
		  m_buf(", ");
		}
	      m_buf("%s", *line_ptr);
	    }
	}
      if(class_schema.class_methods != NULL)
      {
          for(line_ptr = class_schema.class_methods; *line_ptr != NULL; line_ptr++)
          {
              if(line_ptr != class_schema.class_methods || class_schema.methods != NULL)
              {
                  m_buf(", ");
              }
              m_buf(" CLASS %s", *line_ptr);
          }
      }
  }

  /* method files */
  if (class_schema.method_files != NULL)
    {
      char tmp[PATH_MAX + 2];

      m_buf(" FILE ");
      for (line_ptr = class_schema.method_files; *line_ptr != NULL; line_ptr++)
      {
          if(line_ptr != class_schema.method_files)
          {
              m_buf(", ");
          }
          m_buf("'%s'", *line_ptr);
      }
    }

  /* inherit */
  if (class_schema.resolutions != NULL)
    {
      m_buf(" INHERIT ");
      for (line_ptr = class_schema.resolutions; *line_ptr != NULL; line_ptr++)
      {
          if(line_ptr != class_schema.resolutions)
          {
              m_buf(", ");
          }
          m_buf("%s", *line_ptr);
      }
    }

  /* partition */
  if (class_schema.partition != NULL)
    {
      char **first_ptr;

      line_ptr = class_schema.partition;
      m_buf(" %s", *line_ptr);
      line_ptr++;
      if (*line_ptr != NULL)
      {
          m_buf(" (");
          for(first_ptr = line_ptr; *line_ptr != NULL; line_ptr++)
          {
              if(line_ptr != first_ptr)
              {
                  m_buf(", ");
              }
              m_buf("%s", *line_ptr);
          }
          m_buf(")");
      }
    }

  /* comment */
  if (class_schema.comment != NULL && class_schema.comment[0] != '\0')
    {
      DB_VALUE comment_value;
      DB_MAKE_NULL (&comment_value);
      DB_MAKE_STRING (&comment_value, class_schema.comment);

      m_buf(" COMMENT=");
      object_print_common obj_print(m_buf);
      obj_print.describe_value(&comment_value);
      pr_clear_value (&comment_value);
    }
}

/*
 * obj_print_describe_partition_info() -
 *      return: char *
 *  parser(in) :
 *  partinfo(in) :
 *
 */
//--------------------------------------------------------------------------------
void object_print_parser::describe_partition_info(const sm_partition& partinfo){
  DB_VALUE ele;
  char line[SM_MAX_IDENTIFIER_LENGTH + 1], *ptr, *ptr2, *tmp;
  char col_name[DB_MAX_IDENTIFIER_LENGTH + 1];

  m_buf("PARTITION BY ");
  switch (partinfo.partition_type)
    {
    case PT_PARTITION_HASH:
      m_buf("HASH (");
      break;
    case PT_PARTITION_RANGE:
      m_buf("RANGE (");
      break;
    case PT_PARTITION_LIST:
      m_buf("LIST (");
      break;
    }

  tmp = (char *) partinfo.expr;
  assert (tmp != NULL);

  ptr = tmp ? strstr (tmp, "SELECT ") : NULL;
  if (ptr)
    {
      ptr2 = strstr (ptr + 7, " FROM ");
      if (ptr2)
      {
          strncpy(col_name, ptr + 7, CAST_STRLEN(ptr2 - (ptr + 7)));
          col_name[CAST_STRLEN(ptr2 - (ptr + 7))] = 0;
          m_buf("%s) ", col_name);
      }
    }

  if (partinfo.partition_type == PT_PARTITION_HASH)
    {
      if (set_get_element (partinfo.values, 1, &ele) == NO_ERROR)
      {
          m_buf("PARTITIONS %d", DB_GET_INTEGER(&ele));
      }
    }
}

/*
 * Support routines for trigger descriptions found
 * in both class help and trigger help.
 *
 */

/*
 * obj_print_describe_trigger_list () - Describe a list of triggers
 *   return: none
 *   parser(in):
 *   triggers(in): trigger list
 *   strings(in): string list
 *
 * Note :
 *    This description is part of the class help so it contains only
 *    a condensed version of the trigger description.
 */

 void object_print_parser::describe_trigger_list(tr_triglist* triggers, object_print::strlist** strings)
{
  TR_TRIGLIST *t;
  object_print::strlist *new_p;
  char b[8192] = {0};//bSolo: temp hack
  string_buffer sb(sizeof(b), b);

  for (t = triggers; t != NULL; t = t->next)
    {
      sb.clear();
      describe_class_trigger(*t->trigger);

      /* 
       * this used to be add_strlist, but since it is not used in any other
       * place, or ever will be, I unrolled it here.
       */
      new_p = (object_print::strlist*) malloc (sizeof (object_print::strlist));
      if (new_p != NULL)
        {
          new_p->next = *strings;
          *strings = new_p;
          new_p->string = object_print::copy_string(b);
        }
    }
}

 /*
 * obj_print_describe_class_triggers () - This builds an array of trigger
 *                                        descriptions for a class
 *   return: array of trigger description strings ( or NULL) )
 *   parser(in):
 *   class(in): class to examine
 */
const char** object_print_parser::describe_class_triggers (const sm_class& cls, struct db_object& class_mop)
{
  SM_ATTRIBUTE *attribute_p;
  object_print::strlist *strings;
  const char **array = NULL;
  TR_SCHEMA_CACHE *cache;
  int i;

  strings = NULL;

  cache = cls.triggers;
  if (cache != NULL && !tr_validate_schema_cache (cache, &class_mop))
    {
      for (i = 0; i < cache->array_length; i++)
	{
	  describe_trigger_list(cache->triggers[i], &strings);
	}
    }

  for (attribute_p = cls.ordered_attributes; attribute_p != NULL; attribute_p = attribute_p->order_link)
    {
      cache = attribute_p->triggers;
      if (cache != NULL && !tr_validate_schema_cache (cache, &class_mop))
	{
	  for (i = 0; i < cache->array_length; i++)
	    {
	      describe_trigger_list (cache->triggers[i], &strings);
	    }
	}
    }

  for (attribute_p = cls.class_attributes; attribute_p != NULL;
       attribute_p = (SM_ATTRIBUTE *) attribute_p->header.next)
    {
      cache = attribute_p->triggers;
      if (cache != NULL && !tr_validate_schema_cache (cache, &class_mop))
	{
	  for (i = 0; i < cache->array_length; i++)
	    {
	      describe_trigger_list (cache->triggers[i], &strings);
	    }
	}
    }

  if (strings != NULL)
    {
      array = object_print::convert_strlist (strings);
    }
  return array;
}

#if 0 //not used
/*
 * describe_bit_string() -
 *   return: printed string (buffer pointer)
 *   parser(in) : parser context
 *   buffer(in/out) : buffer to place the printed string
 *   str(in) :
 *   str_len(in) :
 *   max_token_length(in) :
 */
PARSER_VARCHAR *
describe_string (const PARSER_CONTEXT * parser, PARSER_VARCHAR * buffer, const char *str, size_t str_length,
		 int max_token_length)
{
  const char *src, *end, *pos;
  int token_length, length;
  const char *delimiter = "'+\n '";

  src = str;
  end = src + str_length;

  /* get current buffer length */
  if (buffer == NULL)
    {
      token_length = 0;
    }
  else
    {
      token_length = buffer->length % (max_token_length + strlen (delimiter));
    }
  for (pos = src; pos < end; pos++, token_length++)
    {
      /* Process the case (*pos == '\'') first. Don't break the string in the middle of internal quotes('') */
      if (*pos == '\'')
	{			/* put '\'' */
	  length = CAST_STRLEN (pos - src + 1);
	  buffer = pt_append_bytes (parser, buffer, src, length);
	  buffer = pt_append_nulstring (parser, buffer, "'");
	  token_length += 1;	/* for appended '\'' */

	  src = pos + 1;	/* advance src pointer */
	}
      else if (token_length > max_token_length)
	{			/* long string */
	  length = CAST_STRLEN (pos - src + 1);
	  buffer = pt_append_bytes (parser, buffer, src, length);
	  buffer = pt_append_nulstring (parser, buffer, delimiter);
	  token_length = 0;	/* reset token_len for the next new token */

	  src = pos + 1;	/* advance src pointer */
	}
    }

  /* dump the remainings */
  length = CAST_STRLEN (pos - src);
  buffer = pt_append_bytes (parser, buffer, src, length);

  return buffer;
}
#endif

#pragma once
#if defined(SERVER_MODE)
#error Does not belong to server module
#endif //defined(SERVER_MODE)
struct class_description;
enum   obj_print_type;
struct db_object;
struct sm_attribute;
struct sm_class;
struct sm_class_constraint;
struct sm_method;
struct sm_method_argument;
struct sm_method_file;
struct sm_method_signature;
struct sm_partition;
struct sm_resolution;
struct tp_domain;
struct tr_trigger;
class string_buffer;

class object_print_parser
{
  private:
    string_buffer &m_buf;
  public:
    object_print_parser (string_buffer &buf)
      : m_buf (buf)
    {}

    void describe_comment (const char *comment);
    void describe_partition_parts (sm_partition *parts, obj_print_type prt_type);
    void describe_identifier (const char *identifier, obj_print_type prt_type);
    void describe_domain (tp_domain *domain, obj_print_type prt_type, bool force_print_collation);
    void describe_argument (sm_method_argument *argument_p, obj_print_type prt_type);
    void describe_method (struct db_object *op, sm_method *method_p, obj_print_type prt_type);
    void describe_signature (sm_method_signature *signature_p, obj_print_type prt_type);
    void describe_attribute (struct db_object *class_p, sm_attribute *attribute_p, bool is_inherited,
			     obj_print_type prt_type, bool force_print_collation);
    void describe_constraint (sm_class *class_p, sm_class_constraint *constraint_p, obj_print_type prt_type);
    void describe_resolution (sm_resolution *resolution_p, obj_print_type prt_type);
    void describe_method_file (struct db_object *class_p, sm_method_file *file_p);
    void describe_class_trigger (tr_trigger *trigger);
    void describe_class (class_description *class_schema, struct db_object *class_op);
    void describe_partition_info (sm_partition *partinfo);

    static const char *describe_trigger_condition_time (tr_trigger *trigger);
    static const char *describe_trigger_action_time (tr_trigger *trigger);

  protected:
};

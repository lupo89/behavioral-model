/* Copyright 2013-present Barefoot Networks, Inc.
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

/*
 * Antonin Bas (antonin@barefootnetworks.com)
 *
 */

#include "bm_sim/P4Objects.h"

using std::unique_ptr;
using std::string;

typedef unsigned char opcode_t;

void P4Objects::build_conditional(const Json::Value &json_expression,
				  Conditional *conditional) {
  if(json_expression.isNull()) return ;
  const string type = json_expression["type"].asString();
  const Json::Value json_value = json_expression["value"];
  if(type == "expression") {
    const string op = json_value["op"].asString();
    const Json::Value json_left = json_value["left"];
    const Json::Value json_right = json_value["right"];

    build_conditional(json_left, conditional);
    build_conditional(json_right, conditional);

    ExprOpcode opcode = ExprOpcodesMap::get_opcode(op);
    conditional->push_back_op(opcode);
  }
  else if(type == "header") {
    header_id_t header_id = get_header_id(json_value.asString());
    conditional->push_back_load_header(header_id);
  }
  else if(type == "field") {
    const string header_name = json_value[0].asString();
    header_id_t header_id = get_header_id(header_name);
    const string field_name = json_value[1].asString();
    int field_offset = get_field_offset(header_id, field_name);
    conditional->push_back_load_field(header_id, field_offset);

    phv_factory.enable_field_arith(header_id, field_offset);
  }
  else if(type == "bool") {
    conditional->push_back_load_bool(json_value.asBool());
  }
  else if(type == "hexstr") {
    conditional->push_back_load_const( Data(json_value.asString()) );
  }
  else {
    assert(0);
  }
}

int P4Objects::init_objects(std::istream &is,
			    const std::set<header_field_pair> &required_fields,
			    const std::set<header_field_pair> &arith_fields) {
  Json::Value cfg_root;
  is >> cfg_root;

  // header types

  const Json::Value &cfg_header_types = cfg_root["header_types"];
  for (const auto &cfg_header_type : cfg_header_types) {
    const string header_type_name = cfg_header_type["name"].asString();
    header_type_id_t header_type_id = cfg_header_type["id"].asInt();
    HeaderType *header_type = new HeaderType(header_type_name,
					     header_type_id);

    const Json::Value &cfg_fields = cfg_header_type["fields"];
    for (const auto cfg_field : cfg_fields) {
      const string field_name = cfg_field[0].asString();
      int field_bit_width = cfg_field[1].asInt();
      header_type->push_back_field(field_name, field_bit_width);
    }

    add_header_type(header_type_name, unique_ptr<HeaderType>(header_type));
  }

  // headers

  const Json::Value &cfg_headers = cfg_root["headers"];
  // size_t num_headers = cfg_headers.size();

  for (const auto &cfg_header : cfg_headers) {

    const string header_name = cfg_header["name"].asString();
    const string header_type_name = cfg_header["header_type"].asString();
    header_id_t header_id = cfg_header["id"].asInt();
    bool metadata = cfg_header["metadata"].asBool();

    HeaderType *header_type = get_header_type(header_type_name);
    header_to_type_map[header_name] = header_type;

    // std::set<int> arith_offsets =
    //   build_arith_offsets(cfg_root["actions"], header_name);

    phv_factory.push_back_header(header_name, header_id, *header_type, metadata);
    phv_factory.disable_all_field_arith(header_id);
    add_header_id(header_name, header_id);
  }

  // header stacks
  
  const Json::Value &cfg_header_stacks = cfg_root["header_stacks"];
  
  for (const auto &cfg_header_stack : cfg_header_stacks) {

    const string header_stack_name = cfg_header_stack["name"].asString();
    const string header_type_name = cfg_header_stack["header_type"].asString();
    header_stack_id_t header_stack_id = cfg_header_stack["id"].asInt();

    HeaderType *header_stack_type = get_header_type(header_type_name);
    header_stack_to_type_map[header_stack_name] = header_stack_type;

    std::vector<header_id_t> header_ids;
    for(const auto &cfg_header_id : cfg_header_stack["header_ids"])
      header_ids.push_back(cfg_header_id.asInt());

    phv_factory.push_back_header_stack(header_stack_name, header_stack_id,
				       *header_stack_type, header_ids);
    add_header_stack_id(header_stack_name, header_stack_id);
  }

  // parsers

  const Json::Value &cfg_parsers = cfg_root["parsers"];
  for (const auto &cfg_parser : cfg_parsers) {

    const string parser_name = cfg_parser["name"].asString();
    p4object_id_t parser_id = cfg_parser["id"].asInt();

    Parser *parser = new Parser(parser_name, parser_id);

    std::unordered_map<string, ParseState *> current_parse_states;

    // parse states

    const Json::Value &cfg_parse_states = cfg_parser["parse_states"];
    for (const auto &cfg_parse_state : cfg_parse_states) {

      const string parse_state_name = cfg_parse_state["name"].asString();
      const p4object_id_t id = cfg_parse_state["id"].asInt();
      // p4object_id_t parse_state_id = cfg_parse_state["id"].asInt();
      ParseState *parse_state = new ParseState(parse_state_name, id);

      const Json::Value &cfg_parser_ops = cfg_parse_state["parser_ops"];
      for(const auto &cfg_parser_op : cfg_parser_ops) {
	const string op_type = cfg_parser_op["op"].asString();
	const Json::Value &cfg_parameters = cfg_parser_op["parameters"];
	if(op_type == "extract") {
	  assert(cfg_parameters.size() == 1);
	  const Json::Value &cfg_extract = cfg_parameters[0];
	  const string extract_type = cfg_extract["type"].asString();
	  const string extract_header = cfg_extract["value"].asString();
	  if(extract_type == "regular") {
	    header_id_t header_id = get_header_id(extract_header);
	    parse_state->add_extract(header_id);
	  }
	  else if(extract_type == "stack") {
	    header_stack_id_t header_stack_id =
	      get_header_stack_id(extract_header);
	    parse_state->add_extract_to_stack(header_stack_id);
	  }
	  else {
	    assert(0 && "parser extract op not supported");
	  }
	}
	else if(op_type == "set") {
	  assert(cfg_parameters.size() == 2);
	  const Json::Value &cfg_dest = cfg_parameters[0];
	  const Json::Value &cfg_src = cfg_parameters[1];

	  const string &dest_type = cfg_dest["type"].asString();
	  assert(dest_type == "field");
	  const auto dest = field_info(cfg_dest["value"][0].asString(),
				       cfg_dest["value"][1].asString());

	  const string &src_type = cfg_src["type"].asString();
	  if(src_type == "field") {
	    const auto src = field_info(cfg_src["value"][0].asString(),
					cfg_src["value"][1].asString());
	    parse_state->add_set_from_field(std::get<0>(dest), std::get<1>(dest),
					    std::get<0>(src), std::get<1>(src));
	  }
	  else if(src_type == "hexstr") {
	    parse_state->add_set_from_data(std::get<0>(dest), std::get<1>(dest),
					   Data(cfg_src["value"].asString()));
	  }
	  else if(src_type == "lookahead") {
	    int offset = cfg_src["value"][0].asInt();
	    int bitwidth = cfg_src["value"][1].asInt();
	    parse_state->add_set_from_lookahead(std::get<0>(dest), std::get<1>(dest),
						offset, bitwidth);
	  }
	  else {
	    assert(0 && "parser set op not supported");
	  }
	}
	else {
	  assert(0 && "parser op not supported");
	}
      }

      // we do not support parser set ops for now

      ParseSwitchKeyBuilder key_builder;
      const Json::Value &cfg_transition_key = cfg_parse_state["transition_key"];
      for (const auto &cfg_key_elem : cfg_transition_key) {
	const string type = cfg_key_elem["type"].asString();
	const auto &cfg_value = cfg_key_elem["value"];
	if(type == "field") {
	  const auto field = field_info(cfg_value[0].asString(),
					cfg_value[1].asString());
	  key_builder.push_back_field(std::get<0>(field), std::get<1>(field));
	}
	else if(type == "stack_field") {
	  const string header_stack_name = cfg_value[0].asString();
	  header_stack_id_t header_stack_id = get_header_stack_id(header_stack_name);
	  HeaderType *header_type = header_stack_to_type_map[header_stack_name];
	  const string field_name = cfg_value[1].asString();
	  int field_offset = header_type->get_field_offset(field_name);
	  key_builder.push_back_stack_field(header_stack_id, field_offset);
	}
	else if(type == "lookahead") {
	  int offset = cfg_value[0].asInt();
	  int bitwidth = cfg_value[1].asInt();
	  key_builder.push_back_lookahead(offset, bitwidth);
	}
	else {
	  assert(0 && "invalid entry in parse state key");
	}
      }

      parse_state->set_key_builder(key_builder);

      parse_states.push_back(unique_ptr<ParseState>(parse_state));
      current_parse_states[parse_state_name] = parse_state;
    }

    for (const auto &cfg_parse_state : cfg_parse_states) {

      const string parse_state_name = cfg_parse_state["name"].asString();
      ParseState *parse_state = current_parse_states[parse_state_name];
      const Json::Value &cfg_transitions = cfg_parse_state["transitions"];
      for(const auto &cfg_transition : cfg_transitions) {

      	const string value_hexstr = cfg_transition["value"].asString();
      	// ignore mask for now
	const string next_state_name = cfg_transition["next_state"].asString();
      	const ParseState *next_state = current_parse_states[next_state_name];

	if(value_hexstr == "default") {
	  parse_state->set_default_switch_case(next_state);
	}
	else {
	  parse_state->add_switch_case(ByteContainer(value_hexstr), next_state);
	}
      }
    }

    const string init_state_name = cfg_parser["init_state"].asString();
    const ParseState *init_state = current_parse_states[init_state_name];
    parser->set_init_state(init_state);

    add_parser(parser_name, unique_ptr<Parser>(parser));
  }

  // deparsers

  const Json::Value &cfg_deparsers = cfg_root["deparsers"];
  for (const auto &cfg_deparser : cfg_deparsers) {

    const string deparser_name = cfg_deparser["name"].asString();
    p4object_id_t deparser_id = cfg_deparser["id"].asInt();
    Deparser *deparser = new Deparser(deparser_name, deparser_id);

    const Json::Value &cfg_ordered_headers = cfg_deparser["order"];
    for (const auto &cfg_header : cfg_ordered_headers) {

      const string header_name = cfg_header.asString();
      deparser->push_back_header(get_header_id(header_name));
    }

    add_deparser(deparser_name, unique_ptr<Deparser>(deparser));
  }

  // calculations

  const Json::Value &cfg_calculations = cfg_root["calculations"];
  for (const auto &cfg_calculation : cfg_calculations) {
    
    const string name = cfg_calculation["name"].asString();
    const p4object_id_t id = cfg_calculation["id"].asInt();
    const string algo = cfg_calculation["algo"].asString();

    BufBuilder builder;
    for (const auto &cfg_field : cfg_calculation["input"]) {
      const string type = cfg_field["type"].asString();
      if(type == "field") {
	header_id_t header_id;
	int offset;
	std::tie(header_id, offset) = field_info(cfg_field["value"][0].asString(),
						 cfg_field["value"][1].asString());
	builder.push_back_field(header_id, offset, get_field_bits(header_id, offset));
      }
      else if(type == "hexstr") {
	builder.push_back_constant(ByteContainer(cfg_field["value"].asString()),
				   cfg_field["bitwidth"].asInt());
      }
      else if(type == "header") {
	header_id_t header_id = get_header_id(cfg_field["value"].asString());
	builder.push_back_header(header_id, get_header_bits(header_id));
      }
      else if(type == "payload") {
	builder.append_payload();
      }
    }

    NamedCalculation *calculation = new NamedCalculation(name, id, builder);
    // I need to find a better way to manage the different selection algos
    // Maybe something similar to what I am doing for action primitives
    // with a register mechanism
    if(algo == "crc16")
      calculation->set_compute_fn(hash::crc16<uint64_t>);
    else if(algo == "csum16")
      calculation->set_compute_fn(hash::cksum16<uint64_t>);
    else
      calculation->set_compute_fn(hash::xxh64<uint64_t>);    
    add_named_calculation(name, unique_ptr<NamedCalculation>(calculation));
  }

  // meter arrays

  const Json::Value &cfg_meter_arrays = cfg_root["meter_arrays"];
  for (const auto &cfg_meter_array : cfg_meter_arrays) {

    const string name = cfg_meter_array["name"].asString();
    const p4object_id_t id = cfg_meter_array["id"].asInt();
    const string type = cfg_meter_array["type"].asString();
    Meter::MeterType meter_type;
    if(type == "packets") {
      meter_type = Meter::MeterType::PACKETS;
    }
    else if(type == "bytes") {
      meter_type = Meter::MeterType::BYTES;
    }
    else {
      assert(0 && "invalid meter type");
    }
    const size_t rate_count = cfg_meter_array["rate_count"].asUInt();
    const size_t size = cfg_meter_array["size"].asUInt();

    MeterArray *meter_array = new MeterArray(name, id,
					     meter_type, rate_count, size);
    add_meter_array(name, unique_ptr<MeterArray>(meter_array));
  }

  // counter arrays

  const Json::Value &cfg_counter_arrays = cfg_root["counter_arrays"];
  for (const auto &cfg_counter_array : cfg_counter_arrays) {

    const string name = cfg_counter_array["name"].asString();
    const p4object_id_t id = cfg_counter_array["id"].asInt();
    const size_t size = cfg_counter_array["size"].asUInt();
    const Json::Value false_value(false);
    const bool is_direct = cfg_counter_array.get("is_direct", false_value).asBool();
    if(is_direct) continue;

    CounterArray *counter_array = new CounterArray(name, id, size);
    add_counter_array(name, unique_ptr<CounterArray>(counter_array));
  }

  // register arrays

  const Json::Value &cfg_register_arrays = cfg_root["register_arrays"];
  for (const auto &cfg_register_array : cfg_register_arrays) {

    const string name = cfg_register_array["name"].asString();
    const p4object_id_t id = cfg_register_array["id"].asInt();
    const size_t size = cfg_register_array["size"].asUInt();
    const int bitwidth = cfg_register_array["bitwidth"].asInt();

    RegisterArray *register_array = new RegisterArray(name, id, size, bitwidth);
    add_register_array(name, unique_ptr<RegisterArray>(register_array));
  }

  // actions

  const Json::Value &cfg_actions = cfg_root["actions"];
  for (const auto &cfg_action : cfg_actions) {

    const string action_name = cfg_action["name"].asString();
    p4object_id_t action_id = cfg_action["id"].asInt();
    std::unique_ptr<ActionFn> action_fn(new ActionFn(action_name, action_id));

    const Json::Value &cfg_primitive_calls = cfg_action["primitives"];
    for (const auto &cfg_primitive_call : cfg_primitive_calls) {
      const string primitive_name = cfg_primitive_call["op"].asString();

      ActionPrimitive_ *primitive =
	ActionOpcodesMap::get_instance()->get_primitive(primitive_name);
      if(!primitive) {
        outstream << "Unknown primitive action: " << primitive_name
		  << std::endl;
	return 1;
      }

      action_fn->push_back_primitive(primitive);

      const Json::Value &cfg_primitive_parameters =
	cfg_primitive_call["parameters"];
      for(const auto &cfg_parameter : cfg_primitive_parameters) {
	const string type = cfg_parameter["type"].asString();

	if(type == "hexstr") {
	  const string value_hexstr = cfg_parameter["value"].asString();
	  action_fn->parameter_push_back_const(Data(value_hexstr));
	}
	else if(type == "runtime_data") {
	  int action_data_offset = cfg_parameter["value"].asInt();
	  action_fn->parameter_push_back_action_data(action_data_offset);
	}
	else if(type == "header") {
	  const string header_name = cfg_parameter["value"].asString();
	  header_id_t header_id = get_header_id(header_name);
	  action_fn->parameter_push_back_header(header_id);

	  // TODO: overkill, needs something more efficient, but looks hard:
	  phv_factory.enable_all_field_arith(header_id);
	}
	else if(type == "field") {
	  const Json::Value &cfg_value_field = cfg_parameter["value"];
	  const string header_name = cfg_value_field[0].asString();
	  header_id_t header_id = get_header_id(header_name);
	  const string field_name = cfg_value_field[1].asString();
	  int field_offset = get_field_offset(header_id, field_name);
	  action_fn->parameter_push_back_field(header_id, field_offset);

	  phv_factory.enable_field_arith(header_id, field_offset);
	}
	else if(type == "calculation") {
	  const string name = cfg_parameter["value"].asString();
	  NamedCalculation *calculation = get_named_calculation(name);
	  action_fn->parameter_push_back_calculation(calculation);
	}
	else if(type == "meter_array") {
	  const string name = cfg_parameter["value"].asString();
	  MeterArray *meter = get_meter_array(name);
	  action_fn->parameter_push_back_meter_array(meter);
	}
	else if(type == "counter_array") {
	  const string name = cfg_parameter["value"].asString();
	  CounterArray *counter = get_counter_array(name);
	  action_fn->parameter_push_back_counter_array(counter);
	}
	else if(type == "register_array") {
	  const string name = cfg_parameter["value"].asString();
	  RegisterArray *register_array = get_register_array(name);
	  action_fn->parameter_push_back_register_array(register_array);
	}
	else if(type == "header_stack") {
	  const string header_stack_name = cfg_parameter["value"].asString();
	  header_id_t header_stack_id = get_header_stack_id(header_stack_name);
	  action_fn->parameter_push_back_header_stack(header_stack_id);
	}
	else {
	  assert(0 && "parameter not supported");
	}
      }
    }
    add_action(action_name, std::move(action_fn));
  }

  // pipelines

  typedef AgeingWriterImpl<TransportNanomsg> MyAgeingWriter;
  const string ageing_ipc_name = "ipc:///tmp/test_bm_ageing.ipc";
  std::shared_ptr<MyAgeingWriter> ageing_writer(new MyAgeingWriter(ageing_ipc_name));
  ageing_monitor = std::unique_ptr<AgeingMonitor>(new AgeingMonitor(ageing_writer));

  const Json::Value &cfg_pipelines = cfg_root["pipelines"];
  for (const auto &cfg_pipeline : cfg_pipelines) {

    const string pipeline_name = cfg_pipeline["name"].asString();
    p4object_id_t pipeline_id = cfg_pipeline["id"].asInt();
    const string first_node_name = cfg_pipeline["init_table"].asString();

    // pipelines -> tables

    const Json::Value &cfg_tables = cfg_pipeline["tables"];
    for (const auto &cfg_table : cfg_tables) {

      const string table_name = cfg_table["name"].asString();
      p4object_id_t table_id = cfg_table["id"].asInt();

      MatchKeyBuilder key_builder;
      const Json::Value &cfg_match_key = cfg_table["key"];
      for (const auto &cfg_key_entry : cfg_match_key) {

	const string match_type = cfg_key_entry["match_type"].asString();
	const Json::Value &cfg_key_field = cfg_key_entry["target"];
	if(match_type == "valid") {
	  const string header_name = cfg_key_field.asString();
	  header_id_t header_id = get_header_id(header_name);
	  key_builder.push_back_valid_header(header_id);
	}
	else {
	  const string header_name = cfg_key_field[0].asString();
	  header_id_t header_id = get_header_id(header_name);
	  const string field_name = cfg_key_field[1].asString();
	  int field_offset = get_field_offset(header_id, field_name);
	  key_builder.push_back_field(header_id, field_offset,
				      get_field_bits(header_id, field_offset));
	}
      }

      const string match_type = cfg_table["match_type"].asString();
      const string table_type = cfg_table["type"].asString();
      const int table_size = cfg_table["max_size"].asInt();
      const Json::Value false_value(false);
      // if attribute is missing, default is false
      const bool with_counters =
	cfg_table.get("with_counters", false_value).asBool();
      const bool with_ageing =
	cfg_table.get("support_timeout", false_value).asBool();

      // TODO: improve this to make it easier to create new kind of tables
      // e.g. like the register mechanism for primitives :)
      std::unique_ptr<MatchActionTable> table;
      if(table_type == "simple") {
	table = MatchActionTable::create_match_action_table<MatchTable>(
          match_type, table_name, table_id, table_size, key_builder,
	  with_counters, with_ageing
        );
      }
      else if(table_type == "indirect") {
	table = MatchActionTable::create_match_action_table<MatchTableIndirect>(
          match_type, table_name, table_id, table_size, key_builder,
	  with_counters, with_ageing
        );
      }
      else if(table_type == "indirect_ws") {
	table = MatchActionTable::create_match_action_table<MatchTableIndirectWS>(
          match_type, table_name, table_id, table_size, key_builder,
	  with_counters, with_ageing
        );

	if(!cfg_table.isMember("selector")) {
	  assert(0 && "indirect_ws tables need to specify a selector");
	}
	const Json::Value &cfg_table_selector = cfg_table["selector"];
	const string selector_algo = cfg_table_selector["algo"].asString();
	// algo is ignore for now, we always use XXH64
	(void) selector_algo;
	const Json::Value &cfg_table_selector_input = cfg_table_selector["input"];

	BufBuilder builder;
	// TODO: I do this kind of thing in a bunch of places, I need to find a
	// nicer way
	for (const auto &cfg_element : cfg_table_selector_input) {

	  const string type = cfg_element["type"].asString();
	  assert(type == "field");  // TODO: other types

	  const Json::Value &cfg_value_field = cfg_element["value"];
	  const string header_name = cfg_value_field[0].asString();
	  header_id_t header_id = get_header_id(header_name);
	  const string field_name = cfg_value_field[1].asString();
	  int field_offset = get_field_offset(header_id, field_name);
	  builder.push_back_field(header_id, field_offset,
				  get_field_bits(header_id, field_offset));
	}
	typedef MatchTableIndirectWS::hash_t hash_t;
	std::unique_ptr<Calculation<hash_t> > calc(new Calculation<hash_t>(builder));
	// I need to find a better way to manage the different selection algos
	// Maybe something similar to what I am doing for action primitives
	// with a register mechanism
	if(selector_algo == "crc16")
	  calc->set_compute_fn(hash::crc16<hash_t>);
	else
	  calc->set_compute_fn(hash::xxh64<hash_t>);
	MatchTableIndirectWS *mt_indirect_ws =
	  static_cast<MatchTableIndirectWS *>(table->get_match_table());
	mt_indirect_ws->set_hash(std::move(calc));
      }
      else {
	assert(0 && "invalid table type");
      }

      if(with_ageing) ageing_monitor->add_table(table->get_match_table());

      add_match_action_table(table_name, std::move(table));
    }

    // pipelines -> conditionals

    const Json::Value &cfg_conditionals = cfg_pipeline["conditionals"];
    for (const auto &cfg_conditional : cfg_conditionals) {

      const string conditional_name = cfg_conditional["name"].asString();
      p4object_id_t conditional_id = cfg_conditional["id"].asInt();
      Conditional *conditional = new Conditional(conditional_name,
						 conditional_id);

      const Json::Value &cfg_expression = cfg_conditional["expression"];
      build_conditional(cfg_expression, conditional);
      conditional->build();

      add_conditional(conditional_name, unique_ptr<Conditional>(conditional));
    }

    // next node resolution for tables

    for (const auto &cfg_table : cfg_tables) {

      const string table_name = cfg_table["name"].asString();
      MatchTableAbstract *table = get_abstract_match_table(table_name);

      const Json::Value &cfg_next_nodes = cfg_table["next_tables"];
      const Json::Value &cfg_actions = cfg_table["actions"];

      for (const auto &cfg_action : cfg_actions) {

	const string action_name = cfg_action.asString();
	const Json::Value &cfg_next_node = cfg_next_nodes[action_name];
	const ControlFlowNode *next_node = nullptr;
	if(!cfg_next_node.isNull()) {
	  next_node = get_control_node(cfg_next_node.asString());
	}
	table->set_next_node(get_action(action_name)->get_id(), next_node);
      }
    }

    // next node resolution for conditionals

    for (const auto &cfg_conditional : cfg_conditionals) {

      const string conditional_name = cfg_conditional["name"].asString();
      Conditional *conditional = get_conditional(conditional_name);

      const Json::Value &cfg_true_next = cfg_conditional["true_next"];
      const Json::Value &cfg_false_next = cfg_conditional["false_next"];

      if(!cfg_true_next.isNull()) {
	ControlFlowNode *next_node = get_control_node(cfg_true_next.asString());
	conditional->set_next_node_if_true(next_node);
      }
      if(!cfg_false_next.isNull()) {
	ControlFlowNode *next_node = get_control_node(cfg_false_next.asString());
	conditional->set_next_node_if_false(next_node);
      }
    }

    ControlFlowNode *first_node = get_control_node(first_node_name);
    Pipeline *pipeline = new Pipeline(pipeline_name, pipeline_id, first_node);
    add_pipeline(pipeline_name, unique_ptr<Pipeline>(pipeline));
  }

  // checksums

  const Json::Value &cfg_checksums = cfg_root["checksums"];
  for (const auto &cfg_checksum : cfg_checksums) {

    const string checksum_name = cfg_checksum["name"].asString();
    p4object_id_t checksum_id = cfg_checksum["id"].asInt();
    const string checksum_type = cfg_checksum["type"].asString();

    const Json::Value &cfg_cksum_field = cfg_checksum["target"];
    const string header_name = cfg_cksum_field[0].asString();
    header_id_t header_id = get_header_id(header_name);
    const string field_name = cfg_cksum_field[1].asString();
    int field_offset = get_field_offset(header_id, field_name);

    Checksum *checksum;
    if(checksum_type == "ipv4") {
      checksum = new IPv4Checksum(checksum_name, checksum_id,
				  header_id, field_offset);
    }
    else {
      assert(checksum_type == "generic");
      const string calculation_name = cfg_checksum["calculation"].asString();
      NamedCalculation *calculation = get_named_calculation(calculation_name);
      checksum = new CalcBasedChecksum(checksum_name, checksum_id,
				       header_id, field_offset, calculation);
    }

    checksums.push_back(unique_ptr<Checksum>(checksum));

    for(auto it = deparsers.begin(); it != deparsers.end(); ++it) {
      it->second->add_checksum(checksum);
    }
  }

  // learn lists

  learn_engine = std::unique_ptr<LearnEngine>(new LearnEngine());

  typedef LearnWriterImpl<TransportNanomsg> MyLearnWriter;
  const string learning_ipc_name = "ipc:///tmp/test_bm_learning.ipc";
  std::shared_ptr<MyLearnWriter> learn_writer;

  const Json::Value &cfg_learn_lists = cfg_root["learn_lists"];

  if(cfg_learn_lists.size() > 0) {
    learn_writer = std::shared_ptr<MyLearnWriter>(new MyLearnWriter(learning_ipc_name));
  }

  for (const auto &cfg_learn_list : cfg_learn_lists) {

    LearnEngine::list_id_t list_id = cfg_learn_list["id"].asInt();
    learn_engine->list_create(list_id, 16); // 16 is max nb of samples
    learn_engine->list_set_learn_writer(list_id, learn_writer);

    const Json::Value &cfg_learn_elements = cfg_learn_list["elements"];
    for (const auto &cfg_learn_element : cfg_learn_elements) {

      const string type = cfg_learn_element["type"].asString();
      assert(type == "field");  // TODO: other types

      const Json::Value &cfg_value_field = cfg_learn_element["value"];
      const string header_name = cfg_value_field[0].asString();
      header_id_t header_id = get_header_id(header_name);
      const string field_name = cfg_value_field[1].asString();
      int field_offset = get_field_offset(header_id, field_name);
      learn_engine->list_push_back_field(list_id, header_id, field_offset);
    }

    learn_engine->list_init(list_id);
  }

  const Json::Value &cfg_field_lists = cfg_root["field_lists"];
  // used only for cloning
  // TODO: some cleanup for learn lists / clone lists / calculation lists
  for (const auto &cfg_field_list : cfg_field_lists) {

    p4object_id_t list_id = cfg_field_list["id"].asInt();
    std::unique_ptr<FieldList> field_list(new FieldList());
    const Json::Value &cfg_elements = cfg_field_list["elements"];
    for (const auto &cfg_element : cfg_elements) {
      
      const string type = cfg_element["type"].asString();
      assert(type == "field");  // TODO: other types

      const Json::Value &cfg_value_field = cfg_element["value"];
      const string header_name = cfg_value_field[0].asString();
      header_id_t header_id = get_header_id(header_name);
      const string field_name = cfg_value_field[1].asString();
      int field_offset = get_field_offset(header_id, field_name);
      field_list->push_back_field(header_id, field_offset);
    }

    add_field_list(list_id, std::move(field_list));
  }

  if(!check_required_fields(required_fields)) {
    return 1;
  }

  // force arith fields

  if(cfg_root.isMember("force_arith")) {

    const Json::Value &cfg_force_arith = cfg_root["force_arith"];

    for (const auto &cfg_field : cfg_force_arith) {

      const auto field = field_info(cfg_field[0].asString(),
				    cfg_field[1].asString());
      phv_factory.enable_field_arith(std::get<0>(field), std::get<1>(field));
    }
  }

  for(const auto &p : arith_fields) {
    if(!field_exists(p.first, p.second)) {
      outstream << "field " << p.first << "." << p.second
		<< " does not exist but required for arith, ignoring\n";
    }
    else {
      const auto field = field_info(p.first, p.second);
      phv_factory.enable_field_arith(std::get<0>(field), std::get<1>(field));
    }
  }

  return 0;
}

void P4Objects::destroy_objects() {
  Packet::unset_phv_factory();
}

void P4Objects::reset_state() {
  // TODO: is this robust?
  for(auto &table : match_action_tables_map) {
    table.second->get_match_table()->reset_state();
  }
  learn_engine->reset_state();
  ageing_monitor->reset_state();
}

int P4Objects::get_field_offset(header_id_t header_id, const string &field_name) {
  const HeaderType &header_type = phv_factory.get_header_type(header_id);
  return header_type.get_field_offset(field_name);  
}

size_t P4Objects::get_field_bytes(header_id_t header_id, int field_offset) {
  const HeaderType &header_type = phv_factory.get_header_type(header_id);
  return (header_type.get_bit_width(field_offset) + 7) / 8;
}

size_t P4Objects::get_field_bits(header_id_t header_id, int field_offset) {
  const HeaderType &header_type = phv_factory.get_header_type(header_id);
  return header_type.get_bit_width(field_offset);
}

size_t P4Objects::get_header_bits(header_id_t header_id) {
  const HeaderType &header_type = phv_factory.get_header_type(header_id);
  return header_type.get_bit_width();
}

std::tuple<header_id_t, int> P4Objects::field_info(const string &header_name,
						   const string &field_name) {
  header_id_t header_id = get_header_id(header_name);
  return std::make_tuple(header_id, get_field_offset(header_id, field_name));
}

bool
P4Objects::field_exists(const string &header_name,
			const string &field_name) {
  const auto it = header_to_type_map.find(header_name);
  if(it == header_to_type_map.end()) return false;
  const HeaderType *header_type = it->second;
  return (header_type->get_field_offset(field_name) != -1);
}

bool
P4Objects::check_required_fields(const std::set<header_field_pair> &required_fields) {
  bool res = true;
  for(const auto &p : required_fields) {
    if(!field_exists(p.first, p.second)) {
      res = false;
      outstream << "Field " << p.first << "." << p.second
		<< " is required by switch target but is not defined\n";
    }
  }
  return res;
}

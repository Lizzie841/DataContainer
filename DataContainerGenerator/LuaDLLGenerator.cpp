//
// This file provided as part of the DataContainer project
//

#include <string>
#include <vector>
#include <fstream>
#include <filesystem> 
#include <iostream> 
#include <algorithm>
#include <cstring>
#include <set>

#include "parsing.hpp"
#include "code_fragments.hpp"



 void error_to_file(std::string const& file_name) {
	std::fstream fileout;
	fileout.open(file_name, std::ios::out);
	if(fileout.is_open()) {
		fileout << "";
		fileout.close();
	}
}

relationship_object_def const* better_primary_key(relationship_object_def const* oldr, relationship_object_def const* newr) {
	if(oldr == nullptr) {
		return newr;
	}

	if(oldr->is_expandable && !newr->is_expandable)
		return newr;
	if(!oldr->is_expandable && newr->is_expandable)
		return oldr;

	switch(oldr->store_type) {
		case storage_type::contiguous:
		{
			switch(newr->store_type) {
				case storage_type::contiguous:
					return oldr->size <= newr->size ? oldr : newr;
				case storage_type::compactable:
				case storage_type::erasable:
					return oldr;
			}
			break;
		}
		case storage_type::erasable:
		{
			switch(newr->store_type) {
				case storage_type::contiguous:
					return newr;
				case storage_type::erasable:
					return oldr->size <= newr->size ? oldr : newr;
				case storage_type::compactable:
					return oldr;
			}
			break;
		}
		case storage_type::compactable:
		{
			switch(newr->store_type) {
				case storage_type::contiguous:
				case storage_type::erasable:
					return newr;
				case storage_type::compactable:
					return oldr->size <= newr->size ? oldr : newr;
			}
			break;
		}
	}

	return oldr;
}

enum class lua_type_match {
	integer, floating_point, boolean, lua_object, handle_to_integer, opaque
};

inline lua_type_match normalize_type(std::string const& in, std::set<std::string> const& made_types) {
	if(in == "char" || in == "unsigned char" || in == "bool" || in == "int8_t" || in == "uint8_t")
		return lua_type_match::integer;
	if(in == "signed char")
		return lua_type_match::integer;
	if(in == "short" || in == "int16_t" || in == "uint16_t")
		return  lua_type_match::integer;
	if(in == "unsigned short")
		return lua_type_match::integer;
	if(in == "int" || in == "long")
		return lua_type_match::integer;
	if(in == "unsigned int" || in == "unsigned long" || in == "int32_t" || in == "uint32_t")
		return lua_type_match::integer;
	if(in == "size_t" || in == "unsigned long long" || in == "int64_t" || in == "uint64_t")
		return lua_type_match::floating_point;
	if(in == "long long")
		return lua_type_match::floating_point;
	if(in == "float" || in == "double")
		return lua_type_match::floating_point;
	if(in == "lua_reference_type")
		return lua_type_match::lua_object;
	if(made_types.count(in) != 0)
		return lua_type_match::handle_to_integer;
	
	return lua_type_match::opaque;
}

int main(int argc, char *argv[]) {
	if(argc > 1) {
		std::fstream input_file;
		std::string input_file_name(argv[1]);
		input_file.open(argv[1], std::ios::in);

		const std::string full_base_file_name = [otemp = std::string(argv[1])]() mutable {
			if(otemp.length() >= 4 && otemp[otemp.length() - 4] == '.') {
				otemp[otemp.length() - 3] = 'h';
				otemp[otemp.length() - 2] = 'p';
				otemp[otemp.length() - 1] = 'p';
				return otemp;
			}
			return otemp + ".hpp";
		}();
		const std::string base_include_name = [&]() {
			auto sep_pos = full_base_file_name.find_last_of('\\');
			if(sep_pos == std::string::npos)
				sep_pos = full_base_file_name.find_last_of('/');
			if(sep_pos == std::string::npos) {
				return full_base_file_name;
			} else {
				return full_base_file_name.substr(sep_pos + 1);
			}
		}();
		const std::string dll_header_name = [&]() {
			auto sep_pos = full_base_file_name.find_last_of('\\');
			if(sep_pos == std::string::npos)
				sep_pos = full_base_file_name.find_last_of('/');
			if(sep_pos == std::string::npos) {
				return std::string("lua_") + full_base_file_name;
			} else {
				return full_base_file_name.substr(0, sep_pos + 1) + "lua_" + full_base_file_name.substr(sep_pos + 1);
			}
		}();
		std::string dll_source_name = dll_header_name;
		dll_source_name[dll_source_name.length() - 3] = 'c';

		error_record err(input_file_name);

		if(!input_file.is_open()) {
			err.add(row_col_pair{ 0, 0 }, 1000, "Could not open input file");
			std::cout << err.accumulated;
			return -1;
		}


		std::string file_contents((std::istreambuf_iterator<char>(input_file)), std::istreambuf_iterator<char>());

		file_def parsed_file = parse_file(file_contents.c_str(), file_contents.c_str() + file_contents.length(), err);

		input_file.close();

		if(err.accumulated.length() > 0) {
			error_to_file(dll_header_name);
			std::cout << err.accumulated;
			return -1;
		}

		// patchup relationship pointers & other information

		for(auto& r : parsed_file.relationship_objects) {
			if(r.is_relationship) {
				for(auto& l : r.indexed_objects) {
					if(auto linked_object = find_by_name(parsed_file, l.type_name); linked_object) {
						l.related_to = linked_object;
					} else {
						err.add(row_col_pair{ 0, 0 }, 1001, std::string("Could not find object named: ") + l.type_name + " in relationship: " + r.name);
						error_to_file(dll_header_name);
						std::cout << err.accumulated;
						return -1;
					}
					if(l.index == index_type::at_most_one && !l.is_optional && l.multiplicity == 1) {
						r.primary_key.points_to = better_primary_key(r.primary_key.points_to, l.related_to);
						if(r.primary_key.points_to == l.related_to)
							r.primary_key.property_name = l.property_name;
					}

					if(l.multiplicity > 1 && l.index == index_type::many && l.ltype == list_type::list) {
						err.add(row_col_pair{ 0, 0 }, 1002, std::string("Unsupported combination of list type storage with multiplicity > 1 in link ")
							+ l.property_name + " in relationship: " + r.name);
						error_to_file(dll_header_name);
						std::cout << err.accumulated;
						return -1;
					}

					if(l.multiplicity > 1 && l.index == index_type::at_most_one) {
						l.is_distinct = true;
					}
				}



				if(r.indexed_objects.size() == 0) {
					err.add(row_col_pair{ 0, 0 }, 1003, std::string("Relationship: ") + r.name + " is between too few objects");
					error_to_file(dll_header_name);
					std::cout << err.accumulated;
					return -1;
				}


				if(r.force_pk.length() > 0) {
					bool pk_forced = false;
					for(auto& link : r.indexed_objects) {
						if(link.property_name == r.force_pk && link.index == index_type::at_most_one
							&& !link.is_optional && link.multiplicity == 1) {
							r.primary_key.points_to = link.related_to;
							r.primary_key.property_name = link.property_name;
							pk_forced = true;
						}
					}
					if(!pk_forced) {
						err.add(row_col_pair{ 0, 0 }, 1004, std::string("Was unable to use ") + r.force_pk + std::string(" as a primary key for relationship: ") + r.name);
						error_to_file(dll_header_name);
						std::cout << err.accumulated;
						return -1;
					}
				}

				for(auto& link : r.indexed_objects) {
					if(link.index != index_type::none)
						link.related_to->relationships_involved_in.push_back(in_relation_information{ r.name, &link, &r });
				}

				if(r.primary_key.points_to) {
					r.size = r.primary_key.points_to->size;
					r.store_type = storage_type::contiguous;
					r.is_expandable = r.primary_key.points_to->is_expandable;

					for(auto& l : r.indexed_objects) {
						if(r.primary_key == l) {
							l.is_primary_key = true;
						}
					}
				} else {
					if(r.store_type != storage_type::erasable && r.store_type != storage_type::compactable) {
						err.add(row_col_pair{ 0, 0 }, 1005, std::string("Relationship ") + r.name +
							" has no primary key, and thus must have either a compactable or erasable storage type to provide a delete function.");
						error_to_file(dll_header_name);
						std::cout << err.accumulated;
						return -1;
					}
				}

			} // end if is a relationship
		} // patchup relationship pointers loop

		// compile list of objects that need serailization stubs
		std::vector<std::string> needs_serialize;
		std::vector<std::string> needs_load_only;

		for(auto& r : parsed_file.relationship_objects) {
			for(auto& p : r.properties) {
				if(p.type == property_type::object) {
					if(std::find(needs_serialize.begin(), needs_serialize.end(), p.data_type) == needs_serialize.end()) {
						needs_serialize.push_back(p.data_type);
						parsed_file.object_types.push_back(p.data_type);
					}
				}
			}
		}
		for(auto& lt : parsed_file.legacy_types) {
			if(std::find(needs_serialize.begin(), needs_serialize.end(), lt) == needs_serialize.end()
				&& std::find(needs_load_only.begin(), needs_load_only.end(), lt) == needs_load_only.end()) {
				needs_load_only.push_back(lt);
				parsed_file.object_types.push_back(lt);
			}
		}

		// identify vectorizable types
		for(auto& ob : parsed_file.relationship_objects) {
			for(auto& prop : ob.properties) {
				if(prop.type == property_type::other && is_vectorizable_type(parsed_file, prop.data_type))
					prop.type = property_type::vectorizable;
				if(prop.type == property_type::array_other && is_vectorizable_type(parsed_file, prop.data_type))
					prop.type = property_type::array_vectorizable;
			}
		}


		// patch up composite key info
		bool needs_hash_include = false;
		std::vector<int32_t> byte_sizes_need_hash;

		for(auto& ob : parsed_file.relationship_objects) {
			for(auto& cc : ob.composite_indexes) {
				needs_hash_include = true;

				int32_t bits_so_far = 0;
				for(auto& k : cc.component_indexes) {

					for(auto& ri : ob.indexed_objects) {
						if(ri.property_name == k.property_name) {
							k.object_type = ri.type_name;
							ri.is_covered_by_composite_key = true;
							k.multiplicity = ri.multiplicity;
						}
					}

					if(k.object_type.length() == 0) {
						err.add(row_col_pair{ 0, 0 }, 1006, std::string("Indexed link ") + k.property_name + " in composite key " + cc.name +
							" in relationship " + ob.name + " does not refer to a link in the relationship.");
						error_to_file(dll_header_name);
						std::cout << err.accumulated;
						return -1;
					}

					k.bit_position = bits_so_far;
					if(k.property_name == ob.primary_key.property_name)
						cc.involves_primary_key = true;

					if(ob.is_expandable) {
						k.number_of_bits = 32;
						bits_so_far += 32;
					} else {
						k.number_of_bits = 0;
						for(auto sz = ob.size; sz != 0; sz = sz >> 1) {
							++k.number_of_bits;
							bits_so_far += k.multiplicity;
						}
					}
				}

				cc.total_bytes = (bits_so_far + 7) / 8;
				if(cc.total_bytes > 8 &&
					std::find(byte_sizes_need_hash.begin(), byte_sizes_need_hash.end(), cc.total_bytes) == byte_sizes_need_hash.end()) {
					byte_sizes_need_hash.push_back(cc.total_bytes);
				}
			}
		}

		// make prepared queries

		for(auto& q : parsed_file.unprepared_queries) {
			parsed_file.prepared_queries.push_back(make_prepared_definition(parsed_file, q, err));
		}
		if(err.accumulated.length() > 0) {
			error_to_file(dll_header_name);
			std::cout << err.accumulated;
			return -1;
		}

		// compose contents of generated file
		std::string output;
		std::string header_output;

		basic_builder o;

		output += "//\n";
		output += "// This file was automatically generated from: " + std::string(argv[1]) + "\n";
		output += "// EDIT AT YOUR OWN RISK; all changes will be lost upon regeneration\n";
		output += "// NOT SUITABLE FOR USE IN CRITICAL SOFTWARE WHERE LIVES OR LIVELIHOODS DEPEND ON THE CORRECT OPERATION\n";
		output += "//\n";
		output += "\n";
		output += "#include \"lua_" + base_include_name + "\"\n";


		header_output += "#pragma once\n";
		header_output += "\n";
		header_output += "//\n";
		header_output += "// This file was automatically generated from: " + std::string(argv[1]) + "\n";
		header_output += "// EDIT AT YOUR OWN RISK; all changes will be lost upon regeneration\n";
		header_output += "// NOT SUITABLE FOR USE IN CRITICAL SOFTWARE WHERE LIVES OR LIVELIHOODS DEPEND ON THE CORRECT OPERATION\n";
		header_output += "//\n";
		header_output += "\n";
		header_output += "#include <stdint.h>\n";
		header_output += "using lua_reference_type = int32_t;\n";
		header_output += "#include \"" + base_include_name + "\"\n";
		header_output += "#define LUA_LIB\n";
		header_output += "#include \"lua.h\"\n";
		header_output += "#include \"lauxlib.h\"\n";
		header_output += "#ifdef DCON_LUADLL_EXPORTS\n";
		header_output += "#define DCON_LUADLL_API __declspec(dllexport)\n";
		header_output += "#else\n";
		header_output += "#define DCON_LUADLL_API __declspec(dllimport)\n";
		header_output += "#endif\n";

		//extern "C" DCON_LUADLL_API void 

		//open new namespace
		header_output += "\n";
		header_output += "extern DCON_LUADLL_API " + parsed_file.namspace + "::data_container state;\n";
		header_output += "\n";

		auto fn_name = base_include_name.substr(0, base_include_name.length() - 4);
		header_output += "LUALIB_API int32_t luaopen_lua_" + fn_name + "(lua_State *L); \n";

		output += "\n";
		output += "DCON_LUADLL_API " + parsed_file.namspace + "::data_container state;\n";
		output += "\n";

		std::string exports_list;

		std::set<std::string> made_types;

		for(auto& ob : parsed_file.relationship_objects) {
			made_types.insert(ob.name + "_id");
		}
		for(auto& mi : parsed_file.extra_ids) {
			made_types.insert(mi.name);
		}

		for(auto& ob : parsed_file.relationship_objects) {

			//o + substitute("obj", ob.name);
			//@obj@_is_valid

			output += "int32_t " + ob.name + "_is_valid(lua_State *L) { \n";
			output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
			output += "\t bool result = state." + ob.name + "_is_valid(index);\n";
			output += "\t lua_pushboolean(L, result);\n";
			output += "\t return 1;\n";
			output += " }\n";

			exports_list += "\t{\"dcon_" + ob.name + "_is_valid\" , " + ob.name + "_is_valid}, \n";

			output += "int32_t " + ob.name + "_size(lua_State *L) { \n";
			output += "\t auto result = state." + ob.name + "_size();\n";
			output += "\t lua_pushinteger(L, lua_Integer(result));\n";
			output += "\t return 1;\n";
			output += " }\n";

			exports_list += "\t{\"dcon_" + ob.name + "_size\" , " + ob.name + "_size}, \n";

			output += "int32_t " + ob.name + "_resize(lua_State *L) { \n";
			output += "\t auto sz = uint32_t(lua_tointeger(L, 1));\n";
			output += "\t state." + ob.name + "_resize(sz);\n";
			output += "\t return 0;\n";
			output += " }\n";

			exports_list += "\t{\"dcon_" + ob.name + "_resize\" , " + ob.name + "_resize}, \n";

			for(auto& prop : ob.properties) {
				auto norm_property_type = normalize_type(prop.data_type, made_types);
				if(prop.type == property_type::array_bitfield)
					norm_property_type = lua_type_match::boolean;

				if(prop.type == property_type::array_bitfield || prop.type == property_type::array_vectorizable || prop.type == property_type::array_other) {
					auto norm_index_type = normalize_type(prop.array_index_type, made_types);
					std::string index_access_string;
					if(norm_index_type == lua_type_match::handle_to_integer) {
						index_access_string = "\t auto sub_index = " + parsed_file.namspace + "::" + prop.array_index_type + "{" + parsed_file.namspace + "::" + prop.array_index_type + "::value_base_t(lua_tointeger(L, 2))};\n";
					} else {
						index_access_string = "\t auto sub_index = " + prop.array_index_type + "(lua_tointeger(L, 2));\n";
					}
					switch(norm_property_type) {
						case lua_type_match::integer:
							if(prop.hook_get || !prop.is_derived) {
								output += "int32_t " + ob.name + "_get_" + prop.name + "(lua_State *L) { \n";
								output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
								output += index_access_string;
								output += "\t auto result = state." + ob.name + "_get_" + prop.name + "(index, sub_index);\n";
								output += "\t lua_pushinteger(L, lua_Integer(result));\n";
								output += "\t return 1;\n";
								output += " }\n";

								exports_list += "\t{\"dcon_" + ob.name + "_get_" + prop.name + "\" , " + ob.name + "_get_" + prop.name + "}, \n";
							}
							if(prop.hook_set || !prop.is_derived) {
								output += "int32_t " + ob.name + "_set_" + prop.name + "(lua_State *L) { \n";
								output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
								output += index_access_string;
								output += "\t auto data = " + prop.data_type + "(lua_tointeger(L, 2));\n";
								output += "\t state." + ob.name + "_set_" + prop.name + "(index, sub_index, data);\n";
								output += "\t return 0;\n";
								output += " }\n";

								exports_list += "\t{\"dcon_" + ob.name + "_set_" + prop.name + "\" , " + ob.name + "_set_" + prop.name + "}, \n";
							}
							break;
						case lua_type_match::floating_point:
							if(prop.hook_get || !prop.is_derived) {
								output += "int32_t " + ob.name + "_get_" + prop.name + "(lua_State *L) { \n";
								output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
								output += index_access_string;
								output += "\t auto result = state." + ob.name + "_get_" + prop.name + "(index, sub_index);\n";
								output += "\t lua_pushnumber(L, lua_Number(result));\n";
								output += "\t return 1;\n";
								output += " }\n";

								exports_list += "\t{\"dcon_" + ob.name + "_get_" + prop.name + "\" , " + ob.name + "_get_" + prop.name + "}, \n";
							}
							if(prop.hook_set || !prop.is_derived) {
								output += "int32_t " + ob.name + "_set_" + prop.name + "(lua_State *L) { \n";
								output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
								output += index_access_string;
								output += "\t auto data = " + prop.data_type + "(lua_tonumber(L, 2));\n";
								output += "\t state." + ob.name + "_set_" + prop.name + "(index, sub_index, data);\n";
								output += "\t return 0;\n";
								output += " }\n";

								exports_list += "\t{\"dcon_" + ob.name + "_set_" + prop.name + "\" , " + ob.name + "_set_" + prop.name + "}, \n";
							}
							break;
						case lua_type_match::boolean:
							if(prop.hook_get || !prop.is_derived) {
								output += "int32_t " + ob.name + "_get_" + prop.name + "(lua_State *L) { \n";
								output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
								output += index_access_string;
								output += "\t auto result = state." + ob.name + "_get_" + prop.name + "(index, sub_index);\n";
								output += "\t lua_pushboolean(L, result);\n";
								output += "\t return 1;\n";
								output += " }\n";

								exports_list += "\t{\"dcon_" + ob.name + "_get_" + prop.name + "\" , " + ob.name + "_get_" + prop.name + "}, \n";
							}
							if(prop.hook_set || !prop.is_derived) {
								output += "int32_t " + ob.name + "_set_" + prop.name + "(lua_State *L) { \n";
								output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
								output += index_access_string;
								output += "\t auto data = bool(lua_toboolean(L, 2));\n";
								output += "\t state." + ob.name + "_set_" + prop.name + "(index, sub_index, data);\n";
								output += "\t return 0;\n";
								output += " }\n";

								exports_list += "\t{\"dcon_" + ob.name + "_set_" + prop.name + "\" , " + ob.name + "_set_" + prop.name + "}, \n";
							}
							break;
						case lua_type_match::lua_object:
							if(prop.hook_get || !prop.is_derived) {
								output += "int32_t " + ob.name + "_get_" + prop.name + "(lua_State *L) { \n";
								output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
								output += index_access_string;
								output += "\t auto result = state." + ob.name + "_get_" + prop.name + "(index, sub_index);\n";
								output += "\t if(result == 0)\n";
								output += "\t\t lua_pushnil(L);\n";
								output += "\t else\n";
								output += "\t\t lua_rawgeti(L, LUA_REGISTRYINDEX, result ^ LUA_REFNIL);\n";
								output += "\t return 1;\n";
								output += " }\n";

								exports_list += "\t{\"dcon_" + ob.name + "_get_" + prop.name + "\" , " + ob.name + "_get_" + prop.name + "}, \n";
							}
							if(prop.hook_set || !prop.is_derived) {
								output += "int32_t " + ob.name + "_set_" + prop.name + "(lua_State *L) { \n";
								output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
								output += index_access_string;
								output += "\t auto result = state." + ob.name + "_get_" + prop.name + "(index, sub_index);\n";
								output += "\t if(result != 0) luaL_unref(L, LUA_REGISTRYINDEX, LUA_REFNIL ^ result);\n";
								output += "\t auto data = LUA_REFNIL ^ lua_reference_type(luaL_ref(L, LUA_REGISTRYINDEX));\n";
								output += "\t lua_pushnil(L);\n";
								output += "\t state." + ob.name + "_set_" + prop.name + "(index, sub_index, data);\n";
								output += "\t return 0;\n";
								output += " }\n";

								exports_list += "\t{\"dcon_" + ob.name + "_set_" + prop.name + "\" , " + ob.name + "_set_" + prop.name + "}, \n";
							}
							break;
						case lua_type_match::handle_to_integer:
							if(prop.hook_get || !prop.is_derived) {
								output += "int32_t " + ob.name + "_get_" + prop.name + "(lua_State *L) { \n";
								output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
								output += index_access_string;
								output += "\t auto result = state." + ob.name + "_get_" + prop.name + "(index, sub_index).index();\n";
								output += "\t lua_pushinteger(L, lua_Integer(result));\n";
								output += "\t return 1;\n";
								output += " }\n";

								exports_list += "\t{\"dcon_" + ob.name + "_get_" + prop.name + "\" , " + ob.name + "_get_" + prop.name + "}, \n";
							}
							if(prop.hook_set || !prop.is_derived) {
								output += "int32_t " + ob.name + "_set_" + prop.name + "(lua_State *L) { \n";
								output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
								output += index_access_string;
								output += "\t auto data = " + parsed_file.namspace + "::" + prop.data_type + "{" + parsed_file.namspace + "::" + prop.data_type + "::value_base_t(lua_tointeger(L, 2))};\n";
								output += "\t state." + ob.name + "_set_" + prop.name + "(index, sub_index, data);\n";
								output += "\t return 0;\n";
								output += " }\n";

								exports_list += "\t{\"dcon_" + ob.name + "_set_" + prop.name + "\" , " + ob.name + "_set_" + prop.name + "}, \n";
							}
							break;
						case lua_type_match::opaque:
							// currently unexposed
							break;
					}

					if(!prop.is_derived) {
						output += "int32_t " + ob.name + "_get_" + prop.name + "_size(lua_State *L) { \n";
						output += "\t bool result = state." + ob.name + "_get_" + prop.name + "_size();\n";
						output += "\t lua_pushinteger(L, lua_Integer(result));\n";
						output += "\t return 1;\n";
						output += " }\n";

						exports_list += "\t{\"dcon_" + ob.name + "_get_" + prop.name + "_size\" , " + ob.name + "_get_" + prop.name + "_size}, \n";

						output += "int32_t " + ob.name + "_resize_" + prop.name + "(lua_State *L) { \n";
						output += "\t state." + ob.name + "_resize_" + prop.name + "(uint32_t(lua_tointeger(L, 1)));\n";
						output += "\t return 0;\n";
						output += " }\n";

						exports_list += "\t{\"dcon_" + ob.name + "_resize_" + prop.name + "\" , " + ob.name + "_resize_" + prop.name + "}, \n";
					}

				} else if(prop.type == property_type::special_vector) { // TODO on this one

				} else {
					switch(norm_property_type) {
						case lua_type_match::integer:
							if(prop.hook_get || !prop.is_derived) {
								output += "int32_t " + ob.name + "_get_" + prop.name + "(lua_State *L) { \n";
								output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
								output += "\t auto result = state." + ob.name + "_get_" + prop.name + "(index);\n";
								output += "\t lua_pushinteger(L, lua_Integer(result));\n";
								output += "\t return 1;\n";
								output += " }\n";

								exports_list += "\t{\"dcon_" + ob.name + "_get_" + prop.name + "\" , " + ob.name + "_get_" + prop.name + "}, \n";
							}
							if(prop.hook_set || !prop.is_derived) {
								output += "int32_t " + ob.name + "_set_" + prop.name + "(lua_State *L) { \n";
								output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
								output += "\t auto data = " + prop.data_type + "(lua_tointeger(L, 2));\n";
								output += "\t state." + ob.name + "_set_" + prop.name + "(index, data);\n";
								output += "\t return 0;\n";
								output += " }\n";

								exports_list += "\t{\"dcon_" + ob.name + "_set_" + prop.name + "\" , " + ob.name + "_set_" + prop.name + "}, \n";
							}
							break;
						case lua_type_match::floating_point:
							if(prop.hook_get || !prop.is_derived) {
								output += "int32_t " + ob.name + "_get_" + prop.name + "(lua_State *L) { \n";
								output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
								output += "\t auto result = state." + ob.name + "_get_" + prop.name + "(index);\n";
								output += "\t lua_pushnumber(L, lua_Number(result));\n";
								output += "\t return 1;\n";
								output += " }\n";

								exports_list += "\t{\"dcon_" + ob.name + "_get_" + prop.name + "\" , " + ob.name + "_get_" + prop.name + "}, \n";
							}
							if(prop.hook_set || !prop.is_derived) {
								output += "int32_t " + ob.name + "_set_" + prop.name + "(lua_State *L) { \n";
								output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
								output += "\t auto data = " + prop.data_type + "(lua_tonumber(L, 2));\n";
								output += "\t state." + ob.name + "_set_" + prop.name + "(index, data);\n";
								output += "\t return 0;\n";
								output += " }\n";

								exports_list += "\t{\"dcon_" + ob.name + "_set_" + prop.name + "\" , " + ob.name + "_set_" + prop.name + "}, \n";
							}
							break;
						case lua_type_match::boolean:
							if(prop.hook_get || !prop.is_derived) {
								output += "int32_t " + ob.name + "_get_" + prop.name + "(lua_State *L) { \n";
								output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
								output += "\t auto result = state." + ob.name + "_get_" + prop.name + "(index);\n";
								output += "\t lua_pushboolean(L, result);\n";
								output += "\t return 1;\n";
								output += " }\n";

								exports_list += "\t{\"dcon_" + ob.name + "_get_" + prop.name + "\" , " + ob.name + "_get_" + prop.name + "}, \n";
							}
							if(prop.hook_set || !prop.is_derived) {
								output += "int32_t " + ob.name + "_set_" + prop.name + "(lua_State *L) { \n";
								output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
								output += "\t auto data = bool(lua_toboolean(L, 2));\n";
								output += "\t state." + ob.name + "_set_" + prop.name + "(index, data);\n";
								output += "\t return 0;\n";
								output += " }\n";

								exports_list += "\t{\"dcon_" + ob.name + "_set_" + prop.name + "\" , " + ob.name + "_set_" + prop.name + "}, \n";
							}
							break;
						case lua_type_match::lua_object:
							if(prop.hook_get || !prop.is_derived) {
								output += "int32_t " + ob.name + "_get_" + prop.name + "(lua_State *L) { \n";
								output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
								output += "\t auto result = state." + ob.name + "_get_" + prop.name + "(index);\n";
								output += "\t if(result == 0)\n";
								output += "\t\t lua_pushnil(L);\n";
								output += "\t else\n";
								output += "\t\t lua_rawgeti(L, LUA_REGISTRYINDEX, result ^ LUA_REFNIL);\n";
								output += "\t return 1;\n";
								output += " }\n";

								exports_list += "\t{\"dcon_" + ob.name + "_get_" + prop.name + "\" , " + ob.name + "_get_" + prop.name + "}, \n";
							}
							if(prop.hook_set || !prop.is_derived) {
								output += "int32_t " + ob.name + "_set_" + prop.name + "(lua_State *L) { \n";
								output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
								output += "\t auto result = state." + ob.name + "_get_" + prop.name + "(index);\n";
								output += "\t if(result != 0) luaL_unref(L, LUA_REGISTRYINDEX, LUA_REFNIL ^ result);\n";
								output += "\t auto data = LUA_REFNIL ^ lua_reference_type(luaL_ref(L, LUA_REGISTRYINDEX));\n";
								output += "\t lua_pushnil(L);\n";
								output += "\t state." + ob.name + "_set_" + prop.name + "(index, data);\n";
								output += "\t return 0;\n";
								output += " }\n";

								exports_list += "\t{\"dcon_" + ob.name + "_set_" + prop.name + "\" , " + ob.name + "_set_" + prop.name + "}, \n";
							}
							break;
						case lua_type_match::handle_to_integer:
							if(prop.hook_get || !prop.is_derived) {
								output += "int32_t " + ob.name + "_get_" + prop.name + "(lua_State *L) { \n";
								output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
								output += "\t auto result = state." + ob.name + "_get_" + prop.name + "(index).index();\n";
								output += "\t lua_pushinteger(L, lua_Integer(result));\n";
								output += "\t return 1;\n";
								output += " }\n";

								exports_list += "\t{\"dcon_" + ob.name + "_get_" + prop.name + "\" , " + ob.name + "_get_" + prop.name + "}, \n";
							}
							if(prop.hook_set || !prop.is_derived) {
								output += "int32_t " + ob.name + "_set_" + prop.name + "(lua_State *L) { \n";
								output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
								output += "\t auto data = " + parsed_file.namspace + "::" + prop.data_type + "{" + parsed_file.namspace + "::" + prop.data_type + "::value_base_t(lua_tointeger(L, 2))};\n";
								output += "\t state." + ob.name + "_set_" + prop.name + "(index, data);\n";
								output += "\t return 0;\n";
								output += " }\n";

								exports_list += "\t{\"dcon_" + ob.name + "_set_" + prop.name + "\" , " + ob.name + "_set_" + prop.name + "}, \n";
							}
							break;
						case lua_type_match::opaque:
							// currently unexposed
							break;
					}
				}
			} // end: loop over properties

			for(auto& indexed : ob.indexed_objects) {
				if(indexed.index == index_type::at_most_one && ob.primary_key == indexed) {

					output += "int32_t " + ob.name + "_get_" + indexed.property_name + "(lua_State *L) { \n";
					output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
					output += "\t auto result = state." + ob.name + "_get_" + indexed.property_name + "(index).index();\n";
					output += "\t lua_pushinteger(L, lua_Integer(result));\n";
					output += "\t return 1;\n";
					output += " }\n";

					exports_list += "\t{\"dcon_" + ob.name + "_get_" + indexed.property_name + "\" , " + ob.name + "_get_" + indexed.property_name + "}, \n";

					output += "int32_t " + ob.name + "_set_" + indexed.property_name + "(lua_State *L) { \n";
					output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
					output += "\t auto data = " + parsed_file.namspace + "::" + indexed.type_name + "_id{" + parsed_file.namspace + "::" + indexed.type_name + "_id::value_base_t(lua_tointeger(L, 2))};\n";
					output += "\t state." + ob.name + "_set_" + indexed.property_name + "(index, data);\n";
					output += "\t return 0;\n";
					output += " }\n";

					exports_list += "\t{\"dcon_" + ob.name + "_set_" + indexed.property_name + "\" , " + ob.name + "_set_" + indexed.property_name + "}, \n";


					output += "int32_t " + ob.name + "_try_set_" + indexed.property_name + "(lua_State *L) { \n";
					output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
					output += "\t auto data = " + parsed_file.namspace + "::" + indexed.type_name + "_id{" + parsed_file.namspace + "::" + indexed.type_name + "_id::value_base_t(lua_tointeger(L, 2))};\n";
					output += "\t auto result = state." + ob.name + "_try_set_" + indexed.property_name + "(index, data);\n";
					output += "\t lua_pushboolean(L, result);\n";
					output += "\t return 1;\n";
					output += " }\n";

					exports_list += "\t{\"dcon_" + ob.name + "_try_set_" + indexed.property_name + "\" , " + ob.name + "_try_set_" + indexed.property_name + "}, \n";

				} else { // if(indexed.index == index_type::at_most_one ||  index_type::many || unindexed 
					if(indexed.multiplicity == 1) {
						output += "int32_t " + ob.name + "_get_" + indexed.property_name + "(lua_State *L) { \n";
						output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
						output += "\t auto result = state." + ob.name + "_get_" + indexed.property_name + "(index).index();\n";
						output += "\t lua_pushinteger(L, lua_Integer(result));\n";
						output += "\t return 1;\n";
						output += " }\n";

						exports_list += "\t{\"dcon_" + ob.name + "_get_" + indexed.property_name + "\" , " + ob.name + "_get_" + indexed.property_name + "}, \n";

						output += "int32_t " + ob.name + "_set_" + indexed.property_name + "(lua_State *L) { \n";
						output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
						output += "\t auto data = " + parsed_file.namspace + "::" + indexed.type_name + "_id{" + parsed_file.namspace + "::" + indexed.type_name + "_id::value_base_t(lua_tointeger(L, 2))};\n";
						output += "\t state." + ob.name + "_set_" + indexed.property_name + "(index, data);\n";
						output += "\t return 0;\n";
						output += " }\n";

						exports_list += "\t{\"dcon_" + ob.name + "_set_" + indexed.property_name + "\" , " + ob.name + "_set_" + indexed.property_name + "}, \n";


						output += "int32_t " + ob.name + "_try_set_" + indexed.property_name + "(lua_State *L) { \n";
						output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
						output += "\t auto data = " + parsed_file.namspace + "::" + indexed.type_name + "_id{" + parsed_file.namspace + "::" + indexed.type_name + "_id::value_base_t(lua_tointeger(L, 2))};\n";
						output += "\t auto result = state." + ob.name + "_try_set_" + indexed.property_name + "(index, data);\n";
						output += "\t lua_pushboolean(L, result);\n";
						output += "\t return 1;\n";
						output += " }\n";

						exports_list += "\t{\"dcon_" + ob.name + "_try_set_" + indexed.property_name + "\" , " + ob.name + "_try_set_" + indexed.property_name + "}, \n";

					} else {
						output += "int32_t " + ob.name + "_get_" + indexed.property_name + "(lua_State *L) { \n";
						output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
						output += "\t auto mindex = int32_t(lua_tointeger(L, 2));\n";
						output += "\t auto result = state." + ob.name + "_get_" + indexed.property_name + "(index, mindex).index();\n";
						output += "\t lua_pushinteger(L, lua_Integer(result));\n";
						output += "\t return 1;\n";
						output += " }\n";

						exports_list += "\t{\"dcon_" + ob.name + "_get_" + indexed.property_name + "\" , " + ob.name + "_get_" + indexed.property_name + "}, \n";

						output += "int32_t " + ob.name + "_set_" + indexed.property_name + "(lua_State *L) { \n";
						output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
						output += "\t auto mindex = int32_t(lua_tointeger(L, 2));\n";
						output += "\t auto data = " + parsed_file.namspace + "::" + indexed.type_name + "_id{" + parsed_file.namspace + "::" + indexed.type_name + "_id::value_base_t(lua_tointeger(L, 3))};\n";
						output += "\t state." + ob.name + "_set_" + indexed.property_name + "(index, mindex, data);\n";
						output += "\t return 0;\n";
						output += " }\n";

						exports_list += "\t{\"dcon_" + ob.name + "_set_" + indexed.property_name + "\" , " + ob.name + "_set_" + indexed.property_name + "}, \n";


						output += "int32_t " + ob.name + "_try_set_" + indexed.property_name + "(lua_State *L) { \n";
						output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
						output += "\t auto mindex = int32_t(lua_tointeger(L, 2));\n";
						output += "\t auto data = " + parsed_file.namspace + "::" + indexed.type_name + "_id{" + parsed_file.namspace + "::" + indexed.type_name + "_id::value_base_t(lua_tointeger(L, 3))};\n";
						output += "\t auto result = state." + ob.name + "_try_set_" + indexed.property_name + "(index, mindex, data);\n";
						output += "\t lua_pushboolean(L, result);\n";
						output += "\t return 1;\n";
						output += " }\n";

						exports_list += "\t{\"dcon_" + ob.name + "_try_set_" + indexed.property_name + "\" , " + ob.name + "_try_set_" + indexed.property_name + "}, \n";
					}
				}
			} // end: loop over indexed objects

			for(auto& involved_in : ob.relationships_involved_in) {
				if(involved_in.linked_as->index == index_type::at_most_one) {
					output += "int32_t " + ob.name + "_get_" + involved_in.relation_name + "_as_" + involved_in.linked_as->property_name + "(lua_State* L) { \n";
					output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
					output += "\t auto result = state." + ob.name + "_get_" + involved_in.relation_name + "_as_" + involved_in.linked_as->property_name + "(index).index();\n";
					output += "\t lua_pushinteger(L, lua_Integer(result));\n";
					output += "\t return 1;\n";
					output += " }\n";

					exports_list += "\t{\"dcon_" + ob.name + "_get_" + involved_in.relation_name + "_as_" + involved_in.linked_as->property_name + "\" , " + ob.name + "_get_" + involved_in.relation_name + "_as_" + involved_in.linked_as->property_name + "}, \n";

					bool is_only_of_type = true;
					for(auto& ir : involved_in.rel_ptr->indexed_objects) {
						if(ir.type_name == ob.name && ir.property_name != involved_in.linked_as->property_name)
							is_only_of_type = false;
					}
					if(is_only_of_type) {
						output += "int32_t " + ob.name + "_get_" + involved_in.relation_name + "(lua_State* L) { \n";
						output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
						output += "\t auto result = state." + ob.name + "_get_" + involved_in.relation_name + "(index).index();\n";
						output += "\t lua_pushinteger(L, lua_Integer(result));\n";
						output += "\t return 1;\n";
						output += " }\n";

						exports_list += "\t{\"dcon_" + ob.name + "_get_" + involved_in.relation_name + "\" , " + ob.name + "_get_" + involved_in.relation_name + "}, \n";
					} // end: is only of type

				} else if(involved_in.linked_as->index == index_type::many) {
					if(involved_in.linked_as->ltype == list_type::array || involved_in.linked_as->ltype == list_type::std_vector) {
						output += "int32_t " + ob.name + "_get_range_" + involved_in.relation_name + "_as_" + involved_in.linked_as->property_name + "(lua_State* L) { \n";
						output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
						output += "\t auto result = state." + ob.name + "_range_of_" + involved_in.relation_name + "_as_" + involved_in.linked_as->property_name + "(index);\n";
						output += "\t lua_pushinteger(L, lua_Integer(result.second - result.first));\n";
						output += "\t return 1;\n";
						output += " }\n";

						exports_list += "\t{\"dcon_" + ob.name + "_get_range_" + involved_in.relation_name + "_as_" + involved_in.linked_as->property_name + "\" , " + ob.name + "_get_range_" + involved_in.relation_name + "_as_" + involved_in.linked_as->property_name + "}, \n";

						output += "int32_t " + ob.name + "_get_index_" + involved_in.relation_name + "_as_" + involved_in.linked_as->property_name + "(lua_State* L) { \n";
						output += "\t auto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
						output += "\t auto subindex = lua_tointeger(L, 2);\n";
						output += "\t auto result = state." + ob.name + "_range_of_" + involved_in.relation_name + "_as_" + involved_in.linked_as->property_name + "(index);\n";
						output += "\t lua_pushinteger(L, lua_Integer(result.first[subindex].index()));\n";
						output += "\t return 1;\n";
						output += " }\n";

						exports_list += "\t{\"dcon_" + ob.name + "_get_index_" + involved_in.relation_name + "_as_" + involved_in.linked_as->property_name + "\" , " + ob.name + "_get_index_" + involved_in.relation_name + "_as_" + involved_in.linked_as->property_name + "}, \n";
					}


					bool is_only_of_type = true;
					for(auto& ir : involved_in.rel_ptr->indexed_objects) {
						if(ir.type_name == ob.name && ir.property_name != involved_in.linked_as->property_name)
							is_only_of_type = false;
					}
					if(is_only_of_type) {

					}
				}
			} // end: loop over relationships involved in


		} // end getters / setters creation loop over relationships / objects


		output += "\n";


		// creation / deletion routines

		//o + "void pop_back_@obj@()" + block{

		for(auto& cob : parsed_file.relationship_objects) {
			const std::string id_name = cob.name + "_id";
			auto make_pop_back_delete = [&]() {
				output += "int32_t pop_back_" + cob.name + "(lua_State *L) { \n";
				output += "\t if(state." + cob.name + "_size() > 0) {\n";
				output += "\t\t auto index = " + parsed_file.namspace + "::" + cob.name + "_id{" + parsed_file.namspace + "::" + cob.name + "_id::value_base_t(state." + cob.name + "_size()) - 1};\n";
				for(auto& p : cob.properties) {
					if(p.data_type == "lua_reference_type") {
						if(p.type == property_type::array_vectorizable || p.type == property_type::array_other) {
							output += "\t\t for(auto i = state." + cob.name + "_get_" + p.name + "_size(); i-->0; ) {\n";
							if(made_types.count(p.array_index_type) > 0) {
								output += "\t\t\t if(auto result = state." + cob.name + "_get_" + p.name + "(index, " + parsed_file.namspace + "::" + p.array_index_type + "{" + parsed_file.namspace + "::" + p.array_index_type + "::value_base_t(i)}); result != 0) luaL_unref(L, LUA_REGISTRYINDEX, LUA_REFNIL ^ result);\n";
							} else {
								output += "\t\t\t if(auto result = state." + cob.name + "_get_" + p.name + "(index, " + p.array_index_type + "(i)); result != 0) luaL_unref(L, LUA_REGISTRYINDEX, LUA_REFNIL ^ result);\n";
							}
							output += "\t\t }\n";
						} else {
							output += "\t\t if(auto result = state." + cob.name + "_get_" + p.name + "(index); result != 0) luaL_unref(L, LUA_REGISTRYINDEX, LUA_REFNIL ^ result);\n";
						}
					}
				}
				output += "\t state.pop_back_" + cob.name + "();\n";
				output += "\t }\n";
				output += "\t return 0;\n";
				output += " }\n";

				exports_list += "\t{\"dcon_pop_back_" + cob.name + "\" , pop_back_" + cob.name + "}, \n";
			};
			auto make_simple_create = [&]() {
				output += "int32_t create_" + cob.name + "(lua_State* L) { \n";
				output += "\t auto result = state.create_" + cob.name + "();\n";
				output += "\t lua_pushinteger(L, lua_Integer(result.index()));\n";
				output += "\t return 1;\n";
				output += " }\n";

				exports_list += "\t{\"dcon_create_" + cob.name + "\" , create_" + cob.name + "}, \n";
			};
			auto make_delete = [&]() {
				output += "int32_t delete_" + cob.name + "(lua_State *L) { \n";
				output += "\t auto index = " + parsed_file.namspace + "::" + cob.name + "_id{" + parsed_file.namspace + "::" + cob.name + "_id::value_base_t(lua_tointeger(L, 1))};\n";
				for(auto& p : cob.properties) {
					if(p.data_type == "lua_reference_type") {
						if(p.type == property_type::array_vectorizable || p.type == property_type::array_other) {
							output += "\t for(auto i = state." + cob.name + "_get_" + p.name + "_size(); i-->0; ) {\n";
							if(made_types.count(p.array_index_type) > 0) {
								output += "\t\t if(auto result = state." + cob.name + "_get_" + p.name + "(index, " + parsed_file.namspace + "::" + p.array_index_type + "{" + parsed_file.namspace + "::" + p.array_index_type + "::value_base_t(i)}); result != 0) luaL_unref(L, LUA_REGISTRYINDEX, LUA_REFNIL ^ result);\n";
							} else {
								output += "\t\t if(auto result = state." + cob.name + "_get_" + p.name + "(index, " + p.array_index_type + "(i)); result != 0) luaL_unref(L, LUA_REGISTRYINDEX, LUA_REFNIL ^ result);\n";
							}
							output += "\t }\n";
						} else {
							output += "\t if(auto result = state." + cob.name + "_get_" + p.name + "(index); result != 0) luaL_unref(L, LUA_REGISTRYINDEX, LUA_REFNIL ^ result);\n";
						}
					}
				}
				output += "\t state.delete_" + cob.name + "(index);\n";
				output += "\t return 0;\n";
				output += " }\n";

				exports_list += "\t{\"dcon_delete_" + cob.name + "\" , delete_" + cob.name + "}, \n";
			};
			auto make_relation_create = [&]() {
				std::string params;
				int32_t pcount = 1;
				for(auto& i : cob.indexed_objects) {
					if(params.length() != 0)
						params += ", ";
					if(i.multiplicity == 1) {
						params += parsed_file.namspace + "::" + i.type_name + "_id{" + parsed_file.namspace + "::" + i.type_name + "_id::value_base_t(lua_tointeger(L, " + std::to_string(pcount) + "))}";
						pcount++;
					} else {
						params += parsed_file.namspace + "::" + i.type_name + "_id{" + parsed_file.namspace + "::" + i.type_name + "_id::value_base_t(lua_tointeger(L, " + std::to_string(pcount) + "))}";
						pcount++;

						for(int32_t j = 1; j < i.multiplicity; ++j) {
							params += ", " + parsed_file.namspace + "::" + i.type_name + "_id{" + parsed_file.namspace + "::" + i.type_name + "_id::value_base_t(lua_tointeger(L, " + std::to_string(pcount) + "))}";
							pcount++;
						}
					}
				}

				output += "int32_t try_create_" + cob.name + "(lua_State* L) { \n";
				output += "\t auto result = state.try_create_" + cob.name + "(" + params + ");\n";
				output += "\t lua_pushinteger(L, lua_Integer(result.index()));\n";
				output += "\t return 1;\n";
				output += " }\n";

				exports_list += "\t{\"dcon_try_create_" + cob.name + "\" , try_create_" + cob.name + "}, \n";

				output += "int32_t force_create_" + cob.name + "(lua_State* L) { \n";
				output += "\t auto result = state.force_create_" + cob.name + "(" + params + ");\n";
				output += "\t lua_pushinteger(L, lua_Integer(result.index()));\n";
				output += "\t return 1;\n";
				output += " }\n";

				exports_list += "\t{\"dcon_force_create_" + cob.name + "\" , force_create_" + cob.name + "}, \n";
			};

			if(!cob.is_relationship) {
				if(cob.store_type == storage_type::contiguous || cob.store_type == storage_type::compactable) {
					make_pop_back_delete();
					make_simple_create();

					if(cob.store_type == storage_type::compactable) {
						make_delete();
					}

				} else if(cob.store_type == storage_type::erasable) {
					make_delete();
					make_simple_create();
				}
			} else if(cob.primary_key.points_to) { // primary key relationship
				make_delete();
				make_relation_create();
			} else { // non pk relationship
				if(cob.store_type == storage_type::contiguous || cob.store_type == storage_type::compactable) {
					make_pop_back_delete();
					make_relation_create();

					if(cob.store_type == storage_type::compactable) {
						make_delete();
					}
				} else if(cob.store_type == storage_type::erasable) {
					make_delete();
					make_relation_create();
				}
			} // end case relationship no primary key
		} // end creation / deletion reoutines creation loop

		output += "\n";
		//reset function

		output += "int32_t reset(lua_State* L) { \n";
		output += "\t state.reset();\n";
		output += "\t return 0;\n";
		output += " }\n";

		exports_list += "\t{\"dcon_reset\" , reset}, \n";

		for(auto& cob : parsed_file.relationship_objects) {
			for(auto& cc : cob.composite_indexes) {
				std::string params;
				int32_t pcount = 1;
				for(auto& k : cc.component_indexes) {
					if(params.length() > 0)
						params += ", ";
					if(k.multiplicity == 1) {
						params += parsed_file.namspace + "::" + k.object_type + "_id{" + parsed_file.namspace + "::" + k.object_type + "_id::value_base_t(lua_tointeger(L, " + std::to_string(pcount) + "))}";
						pcount++;
					} else {
						params += parsed_file.namspace + "::" + k.object_type + "_id{" + parsed_file.namspace + "::" + k.object_type + "_id::value_base_t(lua_tointeger(L, " + std::to_string(pcount) + "))}";
						pcount++;

						for(int32_t i = 1; i < k.multiplicity; ++i) {
							params += ", " + parsed_file.namspace + "::" + k.object_type + "_id{" + parsed_file.namspace + "::" + k.object_type + "_id::value_base_t(lua_tointeger(L, " + std::to_string(pcount) + "))}";
							pcount++;
						}
					}
				}

				output += "int32_t get_" + cob.name + "_by_" + cc.name + "(lua_State* L) { \n";
				output += "\t auto result = state.get_" + cob.name + "_by_" + cc.name + "(" + params + ");\n";
				output += "\t lua_pushinteger(L, lua_Integer(result.index()));\n";
				output += "\t return 1;\n";
				output += " }\n";

				exports_list += "\t{\"dcon_get_" + cob.name + "_by_" + cc.name + "\" , get_" + cob.name + "_by_" + cc.name + "}, \n";

			}
		}

		output += "luaL_Reg lib_contents[] = {\n";
		output += exports_list;
		output += "{nullptr, nullptr} };\n";
		output += "\n";
		output += "LUALIB_API int32_t luaopen_lua_" + fn_name + "(lua_State *L) { \n";
		output += "\t luaL_register(L, \"lua_" + fn_name + "\", lib_contents);\n";
		output += "\t return 1; \n";
		output += "}\n";

		//newline at end of file
		output += "\n";

		{
			std::fstream fileout;
			fileout.open(dll_source_name, std::ios::out);
			if(fileout.is_open()) {
				fileout << output;
				fileout.close();
			} else {
				std::abort();
			}
		}
		{
			std::fstream fileout;
			fileout.open(dll_header_name, std::ios::out);
			if(fileout.is_open()) {
				fileout << header_output;
				fileout.close();
			} else {
				std::abort();
			}
		}
	}
}

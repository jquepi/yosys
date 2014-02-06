/*
 *  yosys -- Yosys Open SYnthesis Suite
 *
 *  Copyright (C) 2012  Clifford Wolf <clifford@clifford.at>
 *  
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *  
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include "kernel/register.h"
#include "kernel/celltypes.h"
#include "kernel/rtlil.h"
#include "kernel/log.h"

struct DesignPass : public Pass {
	DesignPass() : Pass("design", "save, restore and reset current design") { }
	std::map<std::string, RTLIL::Design*> saved_designs;
	virtual ~DesignPass() {
		for (auto &it : saved_designs)
			delete it.second;
		saved_designs.clear();
	}
	virtual void help()
	{
		//   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
		log("\n");
		log("    design -reset\n");
		log("\n");
		log("Clear the current design.\n");
		log("\n");
		log("\n");
		log("    design -save <name>\n");
		log("\n");
		log("Save the current design under the given name.\n");
		log("\n");
		log("\n");
		log("    design -stash <name>\n");
		log("\n");
		log("Save the current design under the given name and then clear the current design.\n");
		log("\n");
		log("\n");
		log("    design -load <name>\n");
		log("\n");
		log("Reset the current design and load the design previously saved under the given\n");
		log("name.\n");
		log("\n");
		log("\n");
		log("    design -copy-from <name> [-as <new_mod_name>] <selection>\n");
		log("\n");
		log("Copy modules from the specified design into the current one. The selection is\n");
		log("evaluated in the other design.\n");
		log("\n");
		log("\n");
		log("    design -copy-to <name> [-as <new_mod_name>] [selection]\n");
		log("\n");
		log("Copy modules from the current design into the soecified one.\n");
		log("\n");
	}
	virtual void execute(std::vector<std::string> args, RTLIL::Design *design)
	{
		bool got_mode = false;
		bool reset_mode = false;
		RTLIL::Design *copy_from_design = NULL, *copy_to_design = NULL;
		std::string save_name, load_name, as_name;
		std::vector<RTLIL::Module*> copy_src_modules;

		size_t argidx;
		for (argidx = 1; argidx < args.size(); argidx++)
		{
			std::string arg = args[argidx];
			if (!got_mode && args[argidx] == "-reset") {
				got_mode = true;
				reset_mode = true;
				continue;
			}
			if (!got_mode && args[argidx] == "-save" && argidx+1 < args.size()) {
				got_mode = true;
				save_name = args[++argidx];
				continue;
			}
			if (!got_mode && args[argidx] == "-stash" && argidx+1 < args.size()) {
				got_mode = true;
				save_name = args[++argidx];
				reset_mode = true;
				continue;
			}
			if (!got_mode && args[argidx] == "-load" && argidx+1 < args.size()) {
				got_mode = true;
				load_name = args[++argidx];
				if (saved_designs.count(load_name) == 0)
					log_cmd_error("No saved design '%s' found!\n", load_name.c_str());
				continue;
			}
			if (!got_mode && args[argidx] == "-copy-from" && argidx+1 < args.size()) {
				got_mode = true;
				if (saved_designs.count(args[++argidx]) == 0)
					log_cmd_error("No saved design '%s' found!\n", args[argidx].c_str());
				copy_from_design = saved_designs.at(args[argidx]);
				copy_to_design = design;
				continue;
			}
			if (!got_mode && args[argidx] == "-copy-to" && argidx+1 < args.size()) {
				got_mode = true;
				if (saved_designs.count(args[++argidx]) == 0)
					saved_designs[args[argidx]] = new RTLIL::Design;
				copy_to_design = saved_designs.at(args[argidx]);
				copy_from_design = design;
				continue;
			}
			if (copy_from_design != NULL && args[argidx] == "-as" && argidx+1 < args.size()) {
				got_mode = true;
				as_name = args[++argidx];
				continue;
			}
			break;
		}

		if (copy_from_design != NULL)
		{
			if (copy_from_design != design && argidx == args.size())
				cmd_error(args, argidx, "Missing selection.\n");

			RTLIL::Selection sel = design->selection_stack.back();
			if (argidx != args.size()) {
				handle_extra_select_args(this, args, argidx, args.size(), copy_from_design);
				sel = copy_from_design->selection_stack.back();
				copy_from_design->selection_stack.pop_back();
				argidx = args.size();
			}

			for (auto &it : copy_from_design->modules) {
				if (sel.selected_whole_module(it.first)) {
					copy_src_modules.push_back(it.second);
					continue;
				}
				if (sel.selected_module(it.first))
					log_cmd_error("Module %s is only partly selected.\n", RTLIL::id2cstr(it.first));
			}
		}

		extra_args(args, argidx, design, false);

		if (!got_mode)
			cmd_error(args, argidx, "Missing mode argument (-reset, -save, -load, -copy-from, or -copy-to).\n");

		if (copy_to_design != NULL)
		{
			if (!as_name.empty() && copy_src_modules.size() > 1)
				log_cmd_error("Only one module can be selected in combination with -as.\n");

			for (auto mod : copy_src_modules)
			{
				std::string trg_name = as_name.empty() ? mod->name : RTLIL::escape_id(as_name);

				if (copy_to_design->modules.count(trg_name))
					delete copy_to_design->modules.at(trg_name);
				copy_to_design->modules[trg_name] = mod->clone();
				copy_to_design->modules[trg_name]->name = trg_name;
			}
		}

		if (!save_name.empty())
		{
			RTLIL::Design *design_copy = new RTLIL::Design;

			for (auto &it : design->modules)
				design_copy->modules[it.first] = it.second->clone();

			design_copy->selection_stack = design->selection_stack;
			design_copy->selection_vars = design->selection_vars;
			design_copy->selected_active_module = design->selected_active_module;

			if (saved_designs.count(save_name))
				delete saved_designs.at(save_name);
			saved_designs[save_name] = design_copy;
		}

		if (reset_mode || !load_name.empty())
		{
			for (auto &it : design->modules)
				delete it.second;
			design->modules.clear();

			design->selection_stack.clear();
			design->selection_vars.clear();
			design->selected_active_module.clear();

			design->selection_stack.push_back(RTLIL::Selection());
		}

		if (!load_name.empty())
		{
			RTLIL::Design *saved_design = saved_designs.at(load_name);

			for (auto &it : saved_design->modules)
				design->modules[it.first] = it.second->clone();

			design->selection_stack = saved_design->selection_stack;
			design->selection_vars = saved_design->selection_vars;
			design->selected_active_module = saved_design->selected_active_module;
		}
	}
} DesignPass;
 

/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: core script virtual machine
 */

#include "zerocomico/script_vm.h"
#include "zerocomico/script_program.h"

#include <cstdlib>

namespace ZeroComico {

namespace {

static bool isConditionalOpcode(const Common::String &op) {
	return op.equalsIgnoreCase("if_e") || op.equalsIgnoreCase("if_ne") ||
	       op.equalsIgnoreCase("if_g") || op.equalsIgnoreCase("if_ge") ||
	       op.equalsIgnoreCase("if_l") || op.equalsIgnoreCase("if_le");
}

} // namespace

ScriptVM::ScriptVM(ScriptVMHost *host) : _host(host) {
}

void ScriptVM::reset() {
	_variables.clear();
	_arrays.clear();
}

bool ScriptVM::parseInteger(const Common::String &token, int32 &value) const {
	if (token.empty())
		return false;

	char *end = nullptr;
	const long parsed = strtol(token.c_str(), &end, 0);
	if (end == token.c_str() || *end != 0)
		return false;

	value = (int32)parsed;
	return true;
}

bool ScriptVM::splitReference(const Common::String &reference, Common::String &name,
                              Common::String &indexExpression) const {
	const uint32 open = reference.find('(');
	if (open == Common::String::npos || reference.size() < open + 3 ||
	    reference[reference.size() - 1] != ')')
		return false;

	name = reference.substr(0, open);
	indexExpression = reference.substr(open + 1, reference.size() - open - 2);
	return !name.empty() && !indexExpression.empty();
}

bool ScriptVM::getVariable(const Common::String &name, int32 &value) const {
	VariableMap::const_iterator it = _variables.find(name);
	if (it == _variables.end())
		return false;
	value = it->_value;
	return true;
}

bool ScriptVM::resolveValue(const Common::String &token, int32 &value) const {
	Common::String arrayName;
	Common::String indexExpression;
	if (splitReference(token, arrayName, indexExpression)) {
		int32 index = 0;
		if (!resolveValue(indexExpression, index) || index < 0)
			return false;

		ArrayMap::const_iterator it = _arrays.find(arrayName);
		if (it == _arrays.end() || (uint32)index >= it->_value.size())
			return false;
		value = it->_value[(uint32)index];
		return true;
	}

	if (getVariable(token, value))
		return true;

	return parseInteger(token, value);
}

bool ScriptVM::setVariable(const Common::String &reference, int32 value) {
	Common::String arrayName;
	Common::String indexExpression;
	if (splitReference(reference, arrayName, indexExpression)) {
		int32 index = 0;
		if (!resolveValue(indexExpression, index) || index < 0)
			return false;

		ArrayMap::iterator it = _arrays.find(arrayName);
		if (it == _arrays.end() || (uint32)index >= it->_value.size())
			return false;
		it->_value[(uint32)index] = value;
		return true;
	}

	_variables[reference] = value;
	return true;
}

bool ScriptVM::declareVariable(const ScriptInstruction &instruction) {
	if (instruction.args.empty())
		return false;

	int32 value = 0;
	if (instruction.args.size() >= 2 && !resolveValue(instruction.args[1], value))
		return false;

	_variables[instruction.args[0]] = value;
	return true;
}

bool ScriptVM::declareArray(const ScriptInstruction &instruction) {
	if (instruction.args.empty())
		return false;

	Common::Array<int32> values;
	for (uint32 i = 1; i < instruction.args.size(); ++i) {
		int32 value = 0;
		if (!resolveValue(instruction.args[i], value))
			return false;
		values.push_back(value);
	}

	_arrays[instruction.args[0]] = values;
	return true;
}

bool ScriptVM::executeMove(const ScriptInstruction &instruction) {
	if (instruction.args.size() < 2)
		return false;

	int32 value = 0;
	if (!resolveValue(instruction.args[1], value))
		return false;
	return setVariable(instruction.args[0], value);
}

bool ScriptVM::executeArithmetic(const ScriptInstruction &instruction) {
	if (instruction.args.empty())
		return false;

	int32 current = 0;
	if (!resolveValue(instruction.args[0], current))
		return false;

	if (instruction.opcode.equalsIgnoreCase("inc"))
		return setVariable(instruction.args[0], current + 1);
	if (instruction.opcode.equalsIgnoreCase("dec"))
		return setVariable(instruction.args[0], current - 1);

	if (instruction.args.size() < 2)
		return false;

	int32 operand = 0;
	if (!resolveValue(instruction.args[1], operand))
		return false;

	if (instruction.opcode.equalsIgnoreCase("add"))
		return setVariable(instruction.args[0], current + operand);
	if (instruction.opcode.equalsIgnoreCase("sub"))
		return setVariable(instruction.args[0], current - operand);
	if (instruction.opcode.equalsIgnoreCase("mul"))
		return setVariable(instruction.args[0], current * operand);

	return false;
}

bool ScriptVM::evaluateComparison(const ScriptInstruction &instruction, bool &result) const {
	if (instruction.args.size() < 2)
		return false;

	int32 left = 0;
	int32 right = 0;
	if (!resolveValue(instruction.args[0], left) || !resolveValue(instruction.args[1], right))
		return false;

	if (instruction.opcode.equalsIgnoreCase("if_e"))
		result = left == right;
	else if (instruction.opcode.equalsIgnoreCase("if_ne"))
		result = left != right;
	else if (instruction.opcode.equalsIgnoreCase("if_g"))
		result = left > right;
	else if (instruction.opcode.equalsIgnoreCase("if_ge"))
		result = left >= right;
	else if (instruction.opcode.equalsIgnoreCase("if_l"))
		result = left < right;
	else if (instruction.opcode.equalsIgnoreCase("if_le"))
		result = left <= right;
	else
		return false;

	return true;
}

uint32 ScriptVM::skipFalseBranch(const ScriptProgram &program, uint32 pc, uint32 endIndex) const {
	const Common::Array<ScriptInstruction> &instructions = program.instructions();
	uint32 nested = 0;

	for (uint32 i = pc + 1; i < endIndex; ++i) {
		const Common::String &op = instructions[i].opcode;
		if (isConditionalOpcode(op)) {
			++nested;
		} else if (op.equalsIgnoreCase("endif")) {
			if (nested == 0)
				return i + 1;
			--nested;
		} else if (op.equalsIgnoreCase("else") && nested == 0) {
			return i + 1;
		}
	}

	return endIndex;
}

uint32 ScriptVM::skipElseBranch(const ScriptProgram &program, uint32 pc, uint32 endIndex) const {
	const Common::Array<ScriptInstruction> &instructions = program.instructions();
	uint32 nested = 0;

	for (uint32 i = pc + 1; i < endIndex; ++i) {
		const Common::String &op = instructions[i].opcode;
		if (isConditionalOpcode(op)) {
			++nested;
		} else if (op.equalsIgnoreCase("endif")) {
			if (nested == 0)
				return i + 1;
			--nested;
		}
	}

	return endIndex;
}

bool ScriptVM::run(const ScriptProgram &program, uint32 startIndex, uint32 endIndex, uint32 maxSteps) {
	const Common::Array<ScriptInstruction> &instructions = program.instructions();
	if (startIndex > instructions.size())
		return false;

	if (endIndex > instructions.size())
		endIndex = instructions.size();

	uint32 pc = startIndex;
	uint32 steps = 0;

	while (pc < endIndex) {
		if (++steps > maxSteps)
			return false;

		const ScriptInstruction &instruction = instructions[pc];
		const Common::String &op = instruction.opcode;

		if (op.equalsIgnoreCase("Variable")) {
			if (!declareVariable(instruction))
				return false;
			++pc;
			continue;
		}

		if (op.equalsIgnoreCase("array")) {
			if (!declareArray(instruction))
				return false;
			++pc;
			continue;
		}

		if (op.equalsIgnoreCase("mov")) {
			if (!executeMove(instruction))
				return false;
			++pc;
			continue;
		}

		if (op.equalsIgnoreCase("inc") || op.equalsIgnoreCase("dec") ||
		    op.equalsIgnoreCase("add") || op.equalsIgnoreCase("sub") ||
		    op.equalsIgnoreCase("mul")) {
			if (!executeArithmetic(instruction))
				return false;
			++pc;
			continue;
		}

		if (isConditionalOpcode(op)) {
			bool condition = false;
			if (!evaluateComparison(instruction, condition))
				return false;
			pc = condition ? pc + 1 : skipFalseBranch(program, pc, endIndex);
			continue;
		}

		if (op.equalsIgnoreCase("else")) {
			pc = skipElseBranch(program, pc, endIndex);
			continue;
		}

		if (op.equalsIgnoreCase("endif") || op.equalsIgnoreCase("label") ||
		    op.equalsIgnoreCase("begin_thread") || op.equalsIgnoreCase("end_thread")) {
			++pc;
			continue;
		}

		if (op.equalsIgnoreCase("jmp")) {
			if (instruction.args.empty())
				return false;
			const int target = program.labelIndex(instruction.args[0]);
			if (target < 0 || (uint32)target >= endIndex)
				return false;
			pc = (uint32)target + 1;
			continue;
		}

		if (!_host || !_host->executeScriptOpcode(instruction))
			return false;

		++pc;
	}

	return true;
}

} // namespace ZeroComico

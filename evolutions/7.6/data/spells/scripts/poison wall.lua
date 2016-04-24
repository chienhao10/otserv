local combat = createCombatObject()
setCombatParam(combat, COMBAT_PARAM_TYPE, COMBAT_POISONDAMAGE)
setCombatParam(combat, COMBAT_PARAM_DISTANCEEFFECT, CONST_ANI_POISON)
setCombatParam(combat, COMBAT_PARAM_CREATEITEM, 1490)
setCombatFormula(combat, COMBAT_FORMULA_LEVELMAGIC, -0.5, -30, -1.0, 0)

local arr = {
{1, 1, 3, 1, 1}
}

local arrDiag = {
{0, 0, 0, 0, 0, 0, 1},
{0, 0, 0, 0, 0, 1, 1},
{0, 0, 0, 0, 1, 1, 0},
{0, 0, 1, 3, 1, 0, 0},
{0, 1, 1, 0, 0, 0, 0},
{1, 1, 0, 0, 0, 0, 0},
{1, 0, 0, 0, 0, 0, 0},
}

local area = createCombatArea(arr, arrDiag)
setCombatArea(combat, area)

function onCastSpell(cid, var)
	return doCombat(cid, combat, var)
end

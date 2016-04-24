-- simple quests based on uniqueId
-- to make quest create chest on map and set its uniqueId to id of quest item

function onUse(cid, item, frompos, item2, topos)
	prize = item.uid
	count = item.actionid

	container = doPlayerAddItem(cid, 1988, 1)
	if prize > 0 and prize < 7000 then
		queststatus = getPlayerStorageValue(cid,prize)

		if queststatus == -1 then
			if count > 1 then
				doPlayerSendTextMessage(cid,22,'You have found backpack with '.. count ..' of ' .. getItemName(prize) .. '.')
				doAddContainerItem(container, prize, count)
				setPlayerStorageValue(cid,prize,1)
			else
				doPlayerSendTextMessage(cid,22,'You have found a backpack with a ' .. getItemName(prize) .. '.')
				doAddContainerItem(container, prize, count)
				setPlayerStorageValue(cid,prize,1)
			end
		else
			doPlayerSendTextMessage(cid,22,"It is empty.")
		end

		return 1
	else
		return 0
	end
end

import lxml.etree, os, posixpath, re, tmx, misc, math

EXP_GATEWAY = re.compile(r"^(\w+):(\w+)$")
EXP_LOCATION = re.compile(r"^(\d+),(\d+)$")
TRIGGER_GATEWAY = 0
TRIGGER_ITEM = 1
TRIGGER_NPC = 2
#TRIGGER_TRAPDOOR = 3
KEYWORD_TO_TRIGGER_TYPE = {
	"gateway": TRIGGER_GATEWAY,
	"item": TRIGGER_ITEM,
	"npc": TRIGGER_NPC
#	"trapdoor": TRIGGER_TRAPDOOR
}

EVENT_NONE = 0
EVENT_ADVANCE_QUEST_AND_REFRESH = 1
EVENT_ADVANCE_QUEST_AND_TELEPORT = 2
KEYWORD_TO_TRIGGER_EVENT = {
	"advancequestandrefresh": EVENT_ADVANCE_QUEST_AND_REFRESH,
	"advancequestandteleport": EVENT_ADVANCE_QUEST_AND_TELEPORT
}


class Trigger:
	def __init__(self, room, obj):
		self.id = obj.name.lower()
		self.room = room
		self.raw = obj
		self.type = KEYWORD_TO_TRIGGER_TYPE[obj.type]
		
		# deterine quest state
		self.quest = None
		self.minquest = None
		self.maxquest = None
		self.qflag = None
		self.unlockflag = None
		if "quest" in obj.props:
			self.quest = room.map.world.quests.getquest(obj.props["quest"])
			self.minquest = self.quest
			self.maxquest = self.quest
			if "questflag" in obj.props:
				self.qflag = self.quest.flag_dict[obj.props["questflag"]]
		else:
			self.minquest = room.map.world.quests.getquest(obj.props["minquest"]) if "minquest" in obj.props else None
			self.maxquest = room.map.world.quests.getquest(obj.props["maxquest"]) if "maxquest" in obj.props else None
			if self.minquest is not None and self.maxquest is not None:
				assert self.minquest.index <= self.maxquest.index, "MaxQuest > MinQuest for object in map: " + room.map.id
		if self.quest is None and self.minquest is None and self.maxquest is None and room.map.quest is not None:
			self.quest = room.map.quest
			self.minquest = room.map.quest
			self.maxquest = room.map.quest
			self.qflag = self.quest.add_flag_if_undefined(obj.props["questflag"]) if "questflag" in obj.props else None
		if self.quest is None and "unlockflag" in obj.props:
			self.unlockflag = room.map.world.quests.add_flag_if_undefined(obj.props["unlockflag"])
		# type-specific initialization
		
		if self.type == TRIGGER_ITEM:
			itemid = obj.props["id"]
			assert itemid in room.map.world.items.item_dict, "Item is undefined (" + itemid + " ) in map: " + room.map.id
			self.item = room.map.world.items.item_dict[itemid]
			if self.quest is not None:
				if self.qflag is None:
					self.qflag = self.quest.add_flag_if_undefined(self.id)
			elif self.unlockflag is None:
				self.unlockflag = room.map.world.quests.add_flag_if_undefined(self.id)
		
		elif self.type == TRIGGER_GATEWAY:
			m = EXP_GATEWAY.match(obj.props.get("target", ""))
			assert m is not None, "Malformed Gateway Target in Map: " + room.map.id
			self.target_map = m.group(1).lower()
			self.target_gate = m.group(2).lower()
		
		elif self.type == TRIGGER_NPC:
			did = obj.props["id"].lower()
			assert did in room.map.world.dialogs.dialog_dict, "Invalid Dialog ID (" + did + ") in Map: " + room.map.id
			self.dialog = room.map.world.dialogs.dialog_dict[did]
		
		elif self.type == TRIGGER_TRAPDOOR:
			m = EXP_LOCATION.match(obj.props.get("respawn"))
			assert m is not None, "Malformed Respawn Location in Map: " + room.map.id
			x = int(m.group(1))
			y = int(m.group(2))
			self.respawnRoomId = x + room.map.width * y
				
		self.qbegin = self.minquest.index if self.minquest is not None else 0xff
		self.qend = self.maxquest.index if self.maxquest is not None else 0xff
		if self.qflag is not None: self.flagid = self.qflag.gindex
		elif self.unlockflag is not None: self.flagid = self.unlockflag.gindex
		else: self.flagid = 0

		# check for special onTrigger flags
		if "ontrigger" in obj.props:
			triggerEventName = obj.props["ontrigger"].lower()
			assert triggerEventName in KEYWORD_TO_TRIGGER_EVENT
			self.event = KEYWORD_TO_TRIGGER_EVENT[triggerEventName]
		else:
			self.event = EVENT_NONE


	def is_active_for(self, quest):
		if self.qbegin != 0xff and self.qbegin < quest.index: return False
		if self.qend != 0xff and self.qend > quest.index: return False
		return True

	def local_position(self):
		return (self.raw.px + (self.raw.pw >> 1) - 128 * self.room.x, 
				self.raw.py + (self.raw.ph >> 1) - 128 * self.room.y)

	
	def write_trigger_to(self, src):
		src.write("{0x%x,0x%x,0x%x,0x%x,0x%x}" % (self.qbegin, self.qend, self.flagid, self.room.lid, self.event))

	def write_item_to(self, src):
		src.write("{ ")
		self.write_trigger_to(src)
		src.write(",0x%x}, " % self.item.numeric_id)
	
	def write_gateway_to(self, src):
		src.write("{ ")
		self.write_trigger_to(src)
		mapid = self.room.map.world.maps.map_dict[self.target_map].index
		gateid = self.room.map.world.maps.map_dict[self.target_map].gate_dict[self.target_gate].index
		x,y = self.local_position()
		src.write(",0x%x,0x%x,0x%x,0x%x}, " % (mapid, gateid, x, y))
	
	def write_npc_to(self, src):
		src.write("{ ")
		self.write_trigger_to(src)
		x,y = self.local_position()
		src.write(",0x%x,0x%x,0x%x}, " % (self.dialog.index, x, y))



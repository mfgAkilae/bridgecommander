# bcmpfix_mp - script layer of the bcmpfix multiplayer fixes (Python 1.5).
#
# Ticked by bcmpfix (dinput.dll) on the main thread; does nothing outside
# multiplayer games. Two jobs:
#
# 1. Host-authoritative torpedo hits: the host reports every torpedo hit on a
#    ship (target, shooter, world hit point). MP clients ignore their own
#    torpedo/ship collisions (DLL hook) and replay the host's hit with their
#    local copy of the torpedo at the reported point, so every player sees the
#    same hits and misses as the host, which decides the damage anyway.
#
# 2. Latency measurement for latency compensation: peers ping each other once a
#    second; the DLL advances remote ships by the one-way latency to the peer
#    their state updates come from (host: owning client, client: host).
#
# All players need this file and the DLL; the features follow bcmpfix.ini.
import App
import sys

MSG_BASE = App.MAX_MESSAGE_TYPES + 120
MSG_HIT = MSG_BASE
MSG_PING = MSG_BASE + 1
MSG_PONG = MSG_BASE + 2
MSG_DIRECT = MSG_BASE + 6   # host: "send me and the others your phaser reports directly" (direct.c)
MSG_BANKS = MSG_BASE + 5    # the phaser banks a ship's owner fires, those that hit and those whose
                            # beam is on an object (bitmasks), with the owner's sequence number;
                            # MSG_BASE + 3 (v1.5.0) and + 4 (v1.6.0) are older formats, now ignored

PING_INTERVAL_MS = 1000
LEAD_INTERVAL_MS = 500
LEAD_EXTRA_MS = 30          # half a state update interval plus about one frame
HIT_SEARCH_RADIUS = 250.0   # max distance between host hit point and local torpedo copy
WAIT_MS = 400               # how long a hit report waits for our copy of the torpedo
FIRE_LEAD_TRIM_MS = 15      # RTT/2 from our pings is about this much more than a message's latency
PREDICT_KEEP_MS = 2000      # how long a predicted hit waits for the host's report

g_idMission = None
g_bHostHasMod = 0
g_iDirectHost = -1    # net ID of the host that announced direct phaser reports
g_dRtt = {}                 # net id -> smoothed round trip time in ms
g_iNextPing = 0
g_iNextLead = 0
g_dHitSent = {}             # host: torpedo id -> ms of its hit report
g_iErrors = 0
g_dPending = {}             # client: torpedo id -> [deadline ms, attempt, target id, rel x, y, z, hull]
g_lWaiting = []             # client: hit reports without a local copy yet: [deadline ms, target, shooter, rel x, y, z, hull]
g_lPredicted = []           # client: hits our own torpedo copies made: [ms, shooter id, target id, hull, matched, torpedo id]
g_dReplayed = {}            # client: torpedo id -> ms its replay started (their hits are no predictions)
g_dStats = {'predicted': 0, 'pred_ok': 0, 'pred_false': 0, 'pred_hull_diff': 0, 'hits_sent': 0, 'hits_recv': 0, 'replayed': 0, 'no_torpedo': 0, 'no_target': 0, 'expired': 0, 'lost': 0, 'retries': 0, 'infopane_skipped': 0, 'waiting': 0, 'late_found': 0, 'moved_n': 0, 'moved_sum': 0, 'moved_max': 0, 'forced': 0}
g_iNextStats = 0


def Ms():
	import bcmpdll
	return bcmpdll.ms()

def Cfg(sKey):
	import bcmpdll
	return bcmpdll.config(sKey)

def DllLog(sText):
	import bcmpdll
	bcmpdll.dlllog(sText)

def Tick():
	global g_iErrors
	try:
		DoTick()
	except:
		import sys
		g_iErrors = g_iErrors + 1
		if g_iErrors <= 10:
			DllLog('bcmpfix_mp: %s %s' % (str(sys.exc_type), str(sys.exc_value)))

def DoTick():
	global g_iNextPing, g_iNextLead, g_iNextStats
	PatchConditionInRange()
	if not App.g_kUtopiaModule.IsMultiplayer():
		return
	GuardInfoPane()
	LogGuards()
	pNet = App.g_kUtopiaModule.GetNetwork()
	if not pNet:
		return
	import MissionLib
	pMission = MissionLib.GetMission()
	if not pMission:
		return
	if g_idMission != pMission.GetObjID():
		Register(pMission)
	RetryWaiting()
	ExpirePending()
	ExpirePredicted()
	t = Ms()
	if t >= g_iNextPing:
		g_iNextPing = t + PING_INTERVAL_MS
		SendPings(pNet, t)
		import bcmpdll
		if App.g_kUtopiaModule.IsHost() and Cfg('DirectPhaserReports') > 0:
			(pDirect, kDirect) = NewMessage(MSG_DIRECT, 0)
			Finish(pDirect, kDirect)
			pNet.SendTGMessageToGroup('NoMe', pDirect)
		bcmpdll.netrole(App.g_kUtopiaModule.IsHost(), g_iDirectHost == pNet.GetHostID())
	if t >= g_iNextLead:
		g_iNextLead = t + LEAD_INTERVAL_MS
		if Cfg('LatencyCompensation') > 0:
			UpdateLeads(pNet)
	if Cfg('Verbose') > 0 and t >= g_iNextStats:
		g_iNextStats = t + 10000
		import bcmpdll
		DllLog('bcmpfix_mp stats %s rtt=%s torp(suppressed allowed)=%s phasers(gated charged sent)=%s direct(sent received peers)=%s' % (str(g_dStats), str(g_dRtt), bcmpdll.torpstats(), bcmpdll.phaserstats(), bcmpdll.directstats()))

# ---- join fix -------------------------------------------------------------
# The missions' RebuildInfoPane (Mission1/2/3/5Menus) runs when a player ship is
# created and uses MissionMenusShared.g_pInfoPane without checking it. A ship that
# arrives while a client joins, before the pane exists, raises AttributeError: in
# windowed mode the game then freezes in its Python debug console, in fullscreen
# ObjectCreatedHandler stops before ResetEnemyFriendlyGroups (and the small UI font
# set by RebuildInfoPane stays). Skip the rebuild while there is no pane; the game
# rebuilds the pane when it creates it.

GUARDED_MENUS = ('Multiplayer.Episode.Mission1.Mission1Menus', 'Multiplayer.Episode.Mission2.Mission2Menus',
		'Multiplayer.Episode.Mission3.Mission3Menus', 'Multiplayer.Episode.Mission5.Mission5Menus')
g_dGuarded = {}

def GuardInfoPane():
	# called by the DLL from inside the game's __import__: no import statements here
	for sName in GUARDED_MENUS:
		if g_dGuarded.has_key(sName) or not sys.modules.has_key(sName):
			continue
		pMod = sys.modules[sName]
		if pMod is None or not hasattr(pMod, 'RebuildInfoPane'):
			continue
		pMod.RebuildInfoPane = MakeInfoPaneGuard(pMod.RebuildInfoPane)
		g_dGuarded[sName] = 1
		g_lGuardLog.append(sName)

# The menus module is often imported by the failing handler itself, so the DLL calls
# GuardInfoPane right after every import of a "...Menus" module (hook in the game's
# __import__, src/scripterr.c); the tick is only a fallback. A Python-level
# __import__ wrapper does not work here: the game's __import__ takes the caller's
# frame globals and adds a dummy fromlist when they are missing.
g_lGuardLog = []            # names guarded, logged by the next tick

def LogGuards():
	while g_lGuardLog:
		DllLog('bcmpfix_mp: guarded %s.RebuildInfoPane' % g_lGuardLog[0])
		del g_lGuardLog[0]

def MakeInfoPaneGuard(pOrig):
	# Python 1.5 has no closures: pass the original through a default argument
	def RebuildInfoPane(pOrig=pOrig):
		import Multiplayer.MissionMenusShared
		if Multiplayer.MissionMenusShared.g_pInfoPane is None:
			g_dStats['infopane_skipped'] = g_dStats['infopane_skipped'] + 1
			return
		pOrig()
	return RebuildInfoPane

def Register(pMission):
	global g_idMission, g_bHostHasMod
	g_idMission = pMission.GetObjID()
	# client: suppress local torpedo hits only once the host proved (by answering
	# a ping) that it runs bcmpfix and will report its hits
	g_bHostHasMod = 0
	import bcmpdll
	bcmpdll.authority(0)
	App.g_kEventManager.AddBroadcastPythonFuncHandler(App.ET_NETWORK_MESSAGE_EVENT, pMission, __name__ + '.OnNetMessage')
	App.g_kEventManager.AddBroadcastPythonFuncHandler(App.ET_WEAPON_HIT, pMission, __name__ + '.OnWeaponHit')
	DllLog('bcmpfix_mp: handlers registered (host=%d)' % App.g_kUtopiaModule.IsHost())

# ---- messages -------------------------------------------------------------

def NewMessage(iType, bGuaranteed):
	pMessage = App.TGMessage_Create()
	pMessage.SetGuaranteed(bGuaranteed)
	kStream = App.TGBufferStream()
	kStream.OpenBuffer(64)
	kStream.WriteChar(chr(iType))
	return (pMessage, kStream)

def Finish(pMessage, kStream):
	pMessage.SetDataFromStream(kStream)
	kStream.CloseBuffer()

def OnNetMessage(pObject, pEvent):
	try:
		pMessage = pEvent.GetMessage()
		if App.IsNull(pMessage):
			return
		kStream = pMessage.GetBufferStream()
		iType = ord(kStream.ReadChar())
		if iType == MSG_HIT:
			ReceiveHit(kStream)
		elif iType == MSG_PING:
			ReceivePing(pMessage, kStream)
		elif iType == MSG_PONG:
			ReceivePong(pMessage, kStream)
		elif iType == MSG_BANKS:
			ReceiveBanks(pMessage, kStream)
		elif iType == MSG_DIRECT:
			ReceiveDirect(pMessage)
		kStream.Close()
	except:
		import sys
		LogError('OnNetMessage', sys.exc_type, sys.exc_value)

# ---- phaser banks (PhaserHitAuthority) ---------------------------------------------
# The DLL watches which phaser banks of our own ship fire and which of their beams are on
# the target, and calls SendBanks at once when that changes; every other machine fires that
# ship's banks only while we fire them, and shows their hits when ours do.

def SendBanks():
	try:
		import bcmpdll
		sState = bcmpdll.takebanks()
		if not sState or Cfg('PhaserSync') <= 0:
			return
		pNet = App.g_kUtopiaModule.GetNetwork()
		if not pNet:
			return
		lParts = string_split(sState)
		(pMessage, kStream) = NewMessage(MSG_BANKS, 1)
		import string
		kStream.WriteInt(string.atoi(lParts[0]))
		kStream.WriteInt(string.atoi(lParts[1]))
		kStream.WriteInt(string.atoi(lParts[2]))
		kStream.WriteInt(string.atoi(lParts[3]))
		kStream.WriteInt(string.atoi(lParts[4]))
		kStream.WriteInt(string.atoi(lParts[5]))
		Finish(pMessage, kStream)
		if App.g_kUtopiaModule.IsHost():
			pNet.SendTGMessageToGroup('NoMe', pMessage)
		elif g_bHostHasMod:
			pNet.SendTGMessage(pNet.GetHostID(), pMessage)
	except:
		import sys
		LogError('SendBanks', sys.exc_type, sys.exc_value)

def ReceiveDirect(pMessage):
	global g_iDirectHost
	pNet = App.g_kUtopiaModule.GetNetwork()
	if pNet and not App.g_kUtopiaModule.IsHost() and pMessage.GetFromID() == pNet.GetHostID():
		g_iDirectHost = pMessage.GetFromID()

# ---- Conditions.ConditionInRange (issue #27) ---------------------------------------
# The original SetObjects registers the condition's group handlers every time it runs, and
# GroupChanged runs it whenever the group changes. So every change of the group adds one more
# GroupChanged handler, and the next change runs all of them: their number doubles. Dynamic
# music watches the enemy group with such a condition; with ten players the first player had
# 100000 handlers, and every deleted object then scanned them all (seconds per frame).
# Here the handlers of a group are registered once per condition.
g_bConditionPatched = 0

def PatchConditionInRange():
	global g_bConditionPatched
	if g_bConditionPatched:
		return
	g_bConditionPatched = 1
	if Cfg('FixConditionInRange') <= 0:
		return
	try:
		import Conditions.ConditionInRange
		Conditions.ConditionInRange.ConditionInRange.SetObjects = ConditionInRange_SetObjects
		DllLog('bcmpfix_mp: ConditionInRange registers its group handlers once')
	except:
		import sys
		LogError('PatchConditionInRange', sys.exc_type, sys.exc_value)

def GroupKey(pGroup):
	# the C++ object behind a SWIG shadow (several shadows can wrap one group)
	return getattr(pGroup, 'this', None)

def ConditionInRange_SetObjects(self, sObject1, *lsObjectNames):
	pObjects = App.ObjectGroup_ForceToGroup(lsObjectNames)
	sKey = GroupKey(pObjects)
	bSameGroup = sKey is not None and self.__dict__.get('bcmp_sGroupKey') == sKey
	bSameObject = bSameGroup and self.__dict__.get('sObject1') == sObject1
	self.sObject1 = sObject1
	self.pObjects = pObjects
	if not bSameObject:
		self.pSingleObjectGroup = App.ObjectGroup()
		self.pSingleObjectGroup.AddName(self.sObject1)
	self.iNumInside = 0
	self.lsInsideObjects = []
	self.pCodeCondition.SetStatus(0)
	pEvents = App.g_kEventManager
	if not bSameGroup:
		self.pObjects.SetEventFlag(App.ObjectGroup.ENTERED_SET)
		pEvents.AddBroadcastPythonMethodHandler(App.ET_OBJECT_GROUP_OBJECT_ENTERED_SET, self.pEventHandler, 'EnteredSet', self.pObjects)
		self.pObjects.SetEventFlag(App.ObjectGroup.EXITED_SET)
		pEvents.AddBroadcastPythonMethodHandler(App.ET_OBJECT_GROUP_OBJECT_EXITED_SET, self.pEventHandler, 'ExitedSet', self.pObjects)
		self.pObjects.SetEventFlag(App.ObjectGroup.GROUP_CHANGED)
		pEvents.AddBroadcastPythonMethodHandler(App.ET_OBJECT_GROUP_CHANGED, self.pEventHandler, 'GroupChanged', self.pObjects)
		self.bcmp_sGroupKey = sKey
	if not bSameObject:
		self.pSingleObjectGroup.SetEventFlag(App.ObjectGroup.ENTERED_SET)
		pEvents.AddBroadcastPythonMethodHandler(App.ET_OBJECT_GROUP_OBJECT_ENTERED_SET, self.pEventHandler, 'EnteredSet', self.pSingleObjectGroup)
		self.pSingleObjectGroup.SetEventFlag(App.ObjectGroup.EXITED_SET)
		pEvents.AddBroadcastPythonMethodHandler(App.ET_OBJECT_GROUP_OBJECT_EXITED_SET, self.pEventHandler, 'ExitedSet', self.pSingleObjectGroup)
	self.SetupProximitySphere()

def string_split(s):
	import string
	return string.split(s)

def ReceiveBanks(pMessage, kStream):
	import bcmpdll
	iShip = kStream.ReadInt()
	iBits = kStream.ReadInt()
	iTarget = kStream.ReadInt()
	iHits = kStream.ReadInt()
	iOn = kStream.ReadInt()
	iSeq = kStream.ReadInt()
	bcmpdll.setbanks(iShip, iBits, iTarget, iHits, iOn, iSeq)
	if App.g_kUtopiaModule.IsHost():
		# a client's ship: pass it on to the other clients
		pNet = App.g_kUtopiaModule.GetNetwork()
		(pOut, kOut) = NewMessage(MSG_BANKS, 1)
		kOut.WriteInt(iShip)
		kOut.WriteInt(iBits)
		kOut.WriteInt(iTarget)
		kOut.WriteInt(iHits)
		kOut.WriteInt(iOn)
		kOut.WriteInt(iSeq)
		Finish(pOut, kOut)
		pNet.SendTGMessageToGroup('NoMe', pOut)

# ---- latency ------------------------------------------------------------------

def SendPings(pNet, t):
	if App.g_kUtopiaModule.IsHost():
		iLocal = pNet.GetLocalID()
		pList = pNet.GetPlayerList()
		i = 0
		while i < pList.GetNumPlayers():
			iID = pList.GetPlayerAtIndex(i).GetNetID()
			if iID != iLocal:
				SendPing(pNet, iID, t)
			i = i + 1
	else:
		SendPing(pNet, pNet.GetHostID(), t)

def SendPing(pNet, iTo, t):
	(pMessage, kStream) = NewMessage(MSG_PING, 0)
	kStream.WriteInt(t)
	Finish(pMessage, kStream)
	pNet.SendTGMessage(iTo, pMessage)

def ReceivePing(pMessage, kStream):
	t = kStream.ReadInt()
	pNet = App.g_kUtopiaModule.GetNetwork()
	(pReply, kOut) = NewMessage(MSG_PONG, 0)
	kOut.WriteInt(t)
	Finish(pReply, kOut)
	pNet.SendTGMessage(pMessage.GetFromID(), pReply)

def ReceivePong(pMessage, kStream):
	global g_bHostHasMod
	iRtt = Ms() - kStream.ReadInt()
	if iRtt < 0 or iRtt > 5000:
		return
	iFrom = pMessage.GetFromID()
	if g_dRtt.has_key(iFrom):
		g_dRtt[iFrom] = (g_dRtt[iFrom] * 3 + iRtt) / 4
	else:
		g_dRtt[iFrom] = iRtt
	pNet = App.g_kUtopiaModule.GetNetwork()
	if not g_bHostHasMod and not App.g_kUtopiaModule.IsHost() and pNet and iFrom == pNet.GetHostID():
		g_bHostHasMod = 1
		import bcmpdll
		bcmpdll.authority(1)
		DllLog('bcmpfix_mp: host runs bcmpfix (rtt %d ms)' % iRtt)

def UpdateLeads(pNet):
	import bcmpdll
	pGame = App.MultiplayerGame_Cast(App.Game_GetCurrentGame())
	if not pGame:
		return
	pPlayer = pGame.GetPlayer()
	iOwn = App.NULL_ID
	if pPlayer:
		iOwn = pPlayer.GetObjID()
	if App.g_kUtopiaModule.IsHost():
		# updates about a client's ship come from that client
		for iID in g_dRtt.keys():
			pShip = pGame.GetShipFromPlayerID(iID)
			if pShip and pShip.GetObjID() != iOwn:
				bcmpdll.setlead(pShip.GetObjID(), g_dRtt[iID] / 2 + LEAD_EXTRA_MS)
				# a client's fire message reaches us directly
				bcmpdll.setfirelead(pShip.GetObjID(), g_dRtt[iID] / 2 - FIRE_LEAD_TRIM_MS)
	else:
		# all state updates come from the host
		iHost = pNet.GetHostID()
		if not g_dRtt.has_key(iHost) or not pPlayer:
			return
		iLead = g_dRtt[iHost] / 2 + LEAD_EXTRA_MS
		iHop = g_dRtt[iHost] / 2 - FIRE_LEAD_TRIM_MS
		pHostShip = pGame.GetShipFromPlayerID(iHost)
		iHostShip = App.NULL_ID
		if pHostShip:
			iHostShip = pHostShip.GetObjID()
		pSet = pPlayer.GetContainingSet()
		if not pSet:
			return
		pObj = pSet.GetFirstObject()
		if App.IsNull(pObj):
			return
		iFirst = pObj.GetObjID()
		iGuard = 0
		while iGuard < 500:
			pShip = App.ShipClass_Cast(pObj)
			if pShip and pShip.GetObjID() != iOwn:
				bcmpdll.setlead(pShip.GetObjID(), iLead)
				# fire messages of other clients come through the host: two hops (we
				# assume their latency to the host is like ours)
				if pShip.GetObjID() == iHostShip:
					bcmpdll.setfirelead(pShip.GetObjID(), iHop)
				else:
					bcmpdll.setfirelead(pShip.GetObjID(), 2 * iHop)
			pObj = pSet.GetNextObject(pObj.GetObjID())
			if App.IsNull(pObj) or pObj.GetObjID() == iFirst:
				break
			iGuard = iGuard + 1

# ---- torpedo hits ---------------------------------------------------------------

# Event handlers must never raise: an uncaught exception in a game event handler
# opens the blocking "TG Python Exception Debug Console" and freezes the game.
def OnWeaponHit(pObject, pEvent):
	try:
		if App.g_kUtopiaModule.IsHost():
			SendHit(pEvent)
		else:
			pTorp = App.Torpedo_Cast(pEvent.GetSource())
			if pTorp and g_dPending.has_key(pTorp.GetObjID()):
				# a replayed host hit just happened locally: retire that torpedo
				del g_dPending[pTorp.GetObjID()]
				import bcmpdll
				bcmpdll.retire(pTorp.this)
				g_dStats['replayed'] = g_dStats['replayed'] + 1
			elif pTorp and pEvent.GetWeaponType() == 1 and Cfg('PredictTorpedoHits') > 0:
				RecordPrediction(pTorp, pEvent)
	except:
		import sys
		LogError('OnWeaponHit', sys.exc_type, sys.exc_value)

def LogError(sWhere, eType, eValue):
	global g_iErrors
	g_iErrors = g_iErrors + 1
	if g_iErrors <= 20:
		DllLog('bcmpfix_mp %s: %s %s' % (sWhere, str(eType), str(eValue)))

def SendHit(pEvent):
	if not App.g_kUtopiaModule.IsHost() or Cfg('TorpedoHitAuthority') <= 0:
		return
	if pEvent.GetWeaponType() != 1:
		return
	pTarget = App.ShipClass_Cast(pEvent.GetDestination())
	pTorp = App.Torpedo_Cast(pEvent.GetSource())
	if not pTarget or not pTorp:
		return
	iTorp = pTorp.GetObjID()
	t = Ms()
	# the stock collision code may apply one torpedo twice (shield, then the part that
	# bleeds through to the hull). Disruptors fire in pairs, so the events of two
	# projectiles interleave: remember every reported torpedo, not only the last one
	if g_dHitSent.has_key(iTorp) and t - g_dHitSent[iTorp] < 1000:
		return
	if len(g_dHitSent) > 64:
		for iOld in g_dHitSent.keys():
			if t - g_dHitSent[iOld] > 2000:
				del g_dHitSent[iOld]
	g_dHitSent[iTorp] = t
	# GetWorldHitPoint() is not usable from script; at hit time the torpedo sits
	# on the impact point anyway
	kP = pTorp.GetWorldLocation()
	(pMessage, kStream) = NewMessage(MSG_HIT, 1)
	kStream.WriteInt(pTarget.GetObjID())
	kStream.WriteInt(pTorp.GetParentID())
	kStream.WriteFloat(kP.GetX())
	kStream.WriteFloat(kP.GetY())
	kStream.WriteFloat(kP.GetZ())
	kStream.WriteChar(chr(pEvent.IsHullHit()))
	Finish(pMessage, kStream)
	pNet = App.g_kUtopiaModule.GetNetwork()
	if pNet:
		pNet.SendTGMessageToGroup('NoMe', pMessage)
		g_dStats['hits_sent'] = g_dStats['hits_sent'] + 1

def ReceiveHit(kStream):
	iTarget = kStream.ReadInt()
	iShooter = kStream.ReadInt()
	fX = kStream.ReadFloat()
	fY = kStream.ReadFloat()
	fZ = kStream.ReadFloat()
	iHull = ord(kStream.ReadChar())
	if App.g_kUtopiaModule.IsHost() or Cfg('TorpedoHitAuthority') <= 0:
		return
	g_dStats['hits_recv'] = g_dStats['hits_recv'] + 1
	pTarget = App.ShipClass_GetObjectByID(None, iTarget)
	if not pTarget:
		g_dStats['no_target'] = g_dStats['no_target'] + 1
		return
	if MatchPrediction(iShooter, iTarget, iHull):
		return
	# remember the impact point relative to the target: it keeps moving
	kC = pTarget.GetWorldLocation()
	lHit = [iTarget, iShooter, fX - kC.GetX(), fY - kC.GetY(), fZ - kC.GetZ(), iHull]
	if not StartReplay(pTarget, lHit):
		# The fire message of a client's torpedo comes from that client, the hit report
		# from the host, so the report can arrive before our copy exists: wait for it.
		g_lWaiting.append([Ms() + WAIT_MS] + lHit)
		g_dStats['waiting'] = g_dStats['waiting'] + 1

def RecordPrediction(pTorp, pEvent):
	# our copy of the torpedo hit a ship (DLL: PredictTorpedoHits); the host's report for
	# it will follow. The stock code may report a shield and a bleed-through hull event for
	# one torpedo: keep one entry per torpedo.
	pTarget = App.ShipClass_Cast(pEvent.GetDestination())
	if not pTarget:
		return
	iTorp = pTorp.GetObjID()
	if g_dReplayed.has_key(iTorp):
		return
	for lP in g_lPredicted:
		if lP[5] == iTorp:
			return
	g_lPredicted.append([Ms(), pTorp.GetParentID(), pTarget.GetObjID(), pEvent.IsHullHit(), 0, iTorp])
	g_dStats['predicted'] = g_dStats['predicted'] + 1

def MatchPrediction(iShooter, iTarget, iHull):
	t = Ms()
	for lP in g_lPredicted:
		if not lP[4] and lP[1] == iShooter and lP[2] == iTarget and t - lP[0] <= PREDICT_KEEP_MS:
			lP[4] = 1
			g_dStats['pred_ok'] = g_dStats['pred_ok'] + 1
			if (lP[3] != 0) != (iHull != 0):
				g_dStats['pred_hull_diff'] = g_dStats['pred_hull_diff'] + 1
			return 1
	return 0

def ExpirePredicted():
	global g_lPredicted
	t = Ms()
	if len(g_dReplayed) > 64:
		for iID in g_dReplayed.keys():
			if t - g_dReplayed[iID] > 5000:
				del g_dReplayed[iID]
	if not g_lPredicted:
		return
	lKeep = []
	for lP in g_lPredicted:
		if t - lP[0] <= PREDICT_KEEP_MS:
			lKeep.append(lP)
		elif not lP[4]:
			g_dStats['pred_false'] = g_dStats['pred_false'] + 1
			if g_dStats['pred_false'] <= 20:
				pShooter = App.ShipClass_GetObjectByID(None, lP[1])
				pTarget = App.ShipClass_GetObjectByID(None, lP[2])
				sShooter = '?'
				sTarget = '?'
				if pShooter:
					sShooter = pShooter.GetName()
				if pTarget:
					sTarget = pTarget.GetName()
				DllLog('bcmpfix_mp: predicted hit not confirmed by the host: %s on %s (%s)' % (sShooter, sTarget, ('shield', 'hull')[lP[3] != 0]))
	g_lPredicted = lKeep

def StartReplay(pTarget, lHit):
	# lHit: [target id, shooter id, rel x, rel y, rel z, hull]
	kC = pTarget.GetWorldLocation()
	pTorp = FindTorpedo(pTarget, lHit[1], kC.GetX() + lHit[2], kC.GetY() + lHit[3], kC.GetZ() + lHit[4])
	if pTorp is None:
		return 0
	g_dPending[pTorp.GetObjID()] = [Ms() + 250, 0, lHit[0], lHit[2], lHit[3], lHit[4], lHit[5]]
	g_dReplayed[pTorp.GetObjID()] = Ms()
	ReplayApproach(pTorp, pTarget, g_dPending[pTorp.GetObjID()])
	return 1

def RetryWaiting():
	global g_lWaiting
	if not g_lWaiting:
		return
	t = Ms()
	lKeep = []
	for lEntry in g_lWaiting:
		pTarget = App.ShipClass_GetObjectByID(None, lEntry[1])
		if not pTarget:
			g_dStats['no_target'] = g_dStats['no_target'] + 1
		elif StartReplay(pTarget, lEntry[1:]):
			g_dStats['late_found'] = g_dStats['late_found'] + 1
		elif t < lEntry[0]:
			lKeep.append(lEntry)
		else:
			g_dStats['no_torpedo'] = g_dStats['no_torpedo'] + 1
	g_lWaiting = lKeep

def ReplayApproach(pTorp, pTarget, lEntry):
	# Re-fly our copy into the host's impact point: start just outside it on the
	# line from the ship centre and head straight in. The stock collision code
	# (released for this torpedo only) then produces the hit within a frame.
	import bcmpdll
	import math
	kC = pTarget.GetWorldLocation()
	fRX = lEntry[3]
	fRY = lEntry[4]
	fRZ = lEntry[5]
	fLen = math.sqrt(fRX * fRX + fRY * fRY + fRZ * fRZ)
	if fLen < 0.001:
		fRX = 0.0
		fRY = 0.0
		fRZ = 1.0
		fLen = 1.0
	fNX = fRX / fLen
	fNY = fRY / fLen
	fNZ = fRZ / fLen
	kV = pTorp.GetVelocityTG()
	fSpeed = math.sqrt(kV.GetX() * kV.GetX() + kV.GetY() * kV.GetY() + kV.GetZ() * kV.GetZ())
	if fSpeed < 10.0:
		fSpeed = 10.0
	# later attempts start further out and aim a bit deeper
	fBack = fSpeed * 0.06 + 1.0 + lEntry[1] * 2.0
	fDeep = 1.0 - lEntry[1] * 0.15
	fX = kC.GetX() + fRX * fDeep
	fY = kC.GetY() + fRY * fDeep
	fZ = kC.GetZ() + fRZ * fDeep
	fSX = fX + fNX * fBack
	fSY = fY + fNY * fBack
	fSZ = fZ + fNZ * fBack
	if lEntry[1] == 0:
		# how far our copy is moved: distance between its path and the shooter's projectile
		kT = pTorp.GetWorldLocation()
		fD = math.sqrt((kT.GetX() - fSX) * (kT.GetX() - fSX) + (kT.GetY() - fSY) * (kT.GetY() - fSY) + (kT.GetZ() - fSZ) * (kT.GetZ() - fSZ))
		g_dStats['moved_n'] = g_dStats['moved_n'] + 1
		g_dStats['moved_sum'] = g_dStats['moved_sum'] + int(fD * 100)
		if fD * 100 > g_dStats['moved_max']:
			g_dStats['moved_max'] = int(fD * 100)
	pTorp.SetTranslateXYZ(fSX, fSY, fSZ)
	kNewV = App.TGPoint3()
	kNewV.SetXYZ(-fNX * fSpeed, -fNY * fSpeed, -fNZ * fSpeed)
	pTorp.SetVelocity(kNewV)
	pTorp.UpdateNodeOnly()
	# mode 1: host saw a shield hit, mode 2: hull hit - the DLL makes ours match;
	# the start point becomes the start of the hit-test segment
	if lEntry[6]:
		bcmpdll.allowhit(pTorp.GetObjID(), 2, fSX, fSY, fSZ)
	else:
		bcmpdll.allowhit(pTorp.GetObjID(), 1, fSX, fSY, fSZ)

def ExpirePending():
	# replays without a local hit get two more approaches; after that the
	# torpedo is retired anyway (the host decided it hit)
	if not g_dPending:
		return
	import bcmpdll
	t = Ms()
	for iID in g_dPending.keys():
		lEntry = g_dPending[iID]
		if t < lEntry[0]:
			continue
		pTorp = App.Torpedo_GetObjectByID(None, iID)
		pTarget = App.ShipClass_GetObjectByID(None, lEntry[2])
		if pTorp and pTarget and lEntry[1] < 2:
			lEntry[1] = lEntry[1] + 1
			lEntry[0] = t + 250
			g_dStats['retries'] = g_dStats['retries'] + 1
			ReplayApproach(pTorp, pTarget, lEntry)
			continue
		del g_dPending[iID]
		if pTorp and pTarget:
			# last resort: run the collision at once from outside the ship to just inside
			# the impact point, then to the centre (the host may report a point deep inside
			# the hull), then as a shield hit, so the host's hit always shows
			if ForceReplay(pTorp, pTarget, lEntry, 0.5, lEntry[6]) or ForceReplay(pTorp, pTarget, lEntry, 0.0, lEntry[6]) or ForceReplay(pTorp, pTarget, lEntry, 0.0, 0):
				# like a replayed hit: retire our copy (it may still carry damage and stay
				# inside the ship; once that ship explodes the game crashes on it)
				bcmpdll.retire(pTorp.this)
				g_dStats['forced'] = g_dStats['forced'] + 1
				continue
		if pTorp:
			if pTarget and g_dStats['expired'] < 20:
				kT = pTorp.GetWorldLocation()
				kC = pTarget.GetWorldLocation()
				fD = (kT.GetX() - kC.GetX()) * (kT.GetX() - kC.GetX()) + (kT.GetY() - kC.GetY()) * (kT.GetY() - kC.GetY()) + (kT.GetZ() - kC.GetZ()) * (kT.GetZ() - kC.GetZ())
				DllLog('bcmpfix_mp: replay expired: target %s, torpedo %.1f from its centre, host saw %s' % (pTarget.GetName(), fD ** 0.5, ('shield', 'hull')[lEntry[6] != 0]))
			bcmpdll.retire(pTorp.this)
			g_dStats['expired'] = g_dStats['expired'] + 1
		else:
			g_dStats['lost'] = g_dStats['lost'] + 1

def ForceReplay(pTorp, pTarget, lEntry, fDepth, iHull):
	import bcmpdll
	lTry = lEntry[:]
	lTry[6] = iHull
	ReplayApproach(pTorp, pTarget, lTry)
	kC = pTarget.GetWorldLocation()
	pTorp.SetTranslateXYZ(kC.GetX() + lEntry[3] * fDepth, kC.GetY() + lEntry[4] * fDepth, kC.GetZ() + lEntry[5] * fDepth)
	pTorp.UpdateNodeOnly()
	return bcmpdll.forcereplay(pTorp.this, pTarget.this)

def FindTorpedo(pTarget, iShooter, fX, fY, fZ):
	pSet = pTarget.GetContainingSet()
	if not pSet:
		return None
	pObj = pSet.GetFirstObject()
	if App.IsNull(pObj):
		return None
	iFirst = pObj.GetObjID()
	pBest = None
	fBest = HIT_SEARCH_RADIUS * HIT_SEARCH_RADIUS
	iGuard = 0
	while iGuard < 1000:
		pTorp = App.Torpedo_Cast(pObj)
		# a torpedo that already replays another hit is taken: pulse weapons fire in
		# pairs, and both reports would otherwise pick the same nearest copy
		if pTorp and pTorp.GetParentID() == iShooter and not g_dPending.has_key(pTorp.GetObjID()):
			kP = pTorp.GetWorldLocation()
			fDX = kP.GetX() - fX
			fDY = kP.GetY() - fY
			fDZ = kP.GetZ() - fZ
			fD = fDX * fDX + fDY * fDY + fDZ * fDZ
			if fD < fBest:
				fBest = fD
				pBest = pTorp
		pObj = pSet.GetNextObject(pObj.GetObjID())
		if App.IsNull(pObj) or pObj.GetObjID() == iFirst:
			break
		iGuard = iGuard + 1
	return pBest


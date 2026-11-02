Scriptname SoftBodyOpeningLog Hidden
{Temporary in-game logging for tuning SoftBody FO4's openings (not part of the release).

From the console, before or during a 4Stim scene the player is in:
    cgf "SoftBodyOpeningLog.Run"
It waits up to two minutes for the scene, finds her (the actor carrying the genital bones) and him (the actor
with A-Body's penis bones), and while the scene lasts writes a sample about twice a second to
Data\F4SE\SoftBodyOpenings.ini (under MO2: overwrite\F4SE). Each sample: the time, the opening widths, each
genital bone's distance from its anchor (its spring's travel is the change from the first sample), and the
penis tip's distance to the openings. Distances are in game units, node to node.
Needs Garden of Eden Papyrus Script Extender and 4Stim.}

String Function LogFile() Global
	return "SoftBodyOpenings.ini"
EndFunction

; index of the first node of these names the actor's 3D has, or -1
int Function FindNode(String[] akNodes, String asName1, String asName2 = "") Global
	int i = akNodes.Find(asName1)
	if i < 0 && asName2 != ""
		i = akNodes.Find(asName2)
	endif
	return i
EndFunction

Function Run(float afMaxSeconds = 900.0) Global
	float started = Utility.GetCurrentRealTime()
	Debug.Notification("SoftBody log: waiting for a 4Stim scene")
	while FourStim.GetPlayerSceneActorCount() < 2 && Utility.GetCurrentRealTime() - started < 120.0
		Utility.Wait(0.5)
	endwhile
	int count = FourStim.GetPlayerSceneActorCount()
	if count < 2
		Debug.Notification("SoftBody log: no scene with two actors, stopped")
		return
	endif

	Actor her = None
	Actor him = None
	String[] herNodes
	String[] himNodes
	int role = 0
	while role < count
		Actor a = Game.GetForm(FourStim.GetPlayerSceneActorID(role)) as Actor
		if a
			String[] nodes = GardenOfEden2.GetModelNodes(a)
			if her == None && nodes.Find("VaginaDB02") >= 0
				her = a
				herNodes = nodes
			elseif him == None && FindNode(nodes, "Penis_05_SMP", "Penis_05") >= 0
				him = a
				himNodes = nodes
			endif
		endif
		role += 1
	endwhile
	if her == None
		Debug.Notification("SoftBody log: no actor in the scene has the SoftBody genital bones")
		return
	endif

	; measurements: a node of hers to a node of hers, or (onHim) the penis tip to a node of hers
	String[] names = new String[40]
	int[] nodeA = new int[40]
	int[] nodeB = new int[40]
	bool[] onHim = new bool[40]
	int n = 0
	; opening widths
	String[] widthA = new String[10]
	String[] widthB = new String[10]
	String[] widthName = new String[10]
	widthName[0] = "lips_LR"
	widthA[0] = "NPC L Pussy02"
	widthB[0] = "NPC R Pussy02"
	widthName[1] = "lips_FB"
	widthA[1] = "Clitoral1"
	widthB[1] = "VaginaB1"
	widthName[2] = "canal1_LR"
	widthA[2] = "VaginaDL01"
	widthB[2] = "VaginaDR01"
	widthName[3] = "canal1_FB"
	widthA[3] = "VaginaDF01"
	widthB[3] = "VaginaDB01"
	widthName[4] = "canal2_LR"
	widthA[4] = "VaginaDL02"
	widthB[4] = "VaginaDR02"
	widthName[5] = "canal2_FB"
	widthA[5] = "VaginaDF02"
	widthB[5] = "VaginaDB02"
	widthName[6] = "canal3_LR"
	widthA[6] = "VaginaDL03"
	widthB[6] = "VaginaDR03"
	widthName[7] = "canal3_FB"
	widthA[7] = "VaginaDF03"
	widthB[7] = "VaginaDB03"
	widthName[8] = "anus_LTRB"
	widthA[8] = "NPC LT Anus2"
	widthB[8] = "NPC RB Anus2"
	widthName[9] = "anus_RTLB"
	widthA[9] = "NPC RT Anus2"
	widthB[9] = "NPC LB Anus2"
	int k = 0
	while k < 10
		names[n] = widthName[k]
		nodeA[n] = herNodes.Find(widthA[k])
		nodeB[n] = herNodes.Find(widthB[k])
		n += 1
		k += 1
	endwhile
	; each moving genital bone from its anchor
	String[] bone = new String[22]
	String[] anchor = new String[22]
	bone[0] = "Clitoral1"
	anchor[0] = "Clitoral"
	bone[1] = "VaginaB1"
	anchor[1] = "VaginaB"
	bone[2] = "NPC L Pussy02"
	anchor[2] = "NPC L Pussy01"
	bone[3] = "NPC R Pussy02"
	anchor[3] = "NPC R Pussy01"
	bone[4] = "VaginaDeep1"
	anchor[4] = "VaginaDeep"
	bone[5] = "VaginaDeep2"
	anchor[5] = "VaginaDeep1"
	bone[6] = "VaginaDB01"
	anchor[6] = "VaginaDB00"
	bone[7] = "VaginaDF01"
	anchor[7] = "VaginaDF00"
	bone[8] = "VaginaDL01"
	anchor[8] = "VaginaDL00"
	bone[9] = "VaginaDR01"
	anchor[9] = "VaginaDR00"
	bone[10] = "VaginaDB02"
	anchor[10] = "VaginaDB00"
	bone[11] = "VaginaDF02"
	anchor[11] = "VaginaDF00"
	bone[12] = "VaginaDL02"
	anchor[12] = "VaginaDL00"
	bone[13] = "VaginaDR02"
	anchor[13] = "VaginaDR00"
	bone[14] = "VaginaDB03"
	anchor[14] = "VaginaDB00"
	bone[15] = "VaginaDF03"
	anchor[15] = "VaginaDF00"
	bone[16] = "VaginaDL03"
	anchor[16] = "VaginaDL00"
	bone[17] = "VaginaDR03"
	anchor[17] = "VaginaDR00"
	bone[18] = "NPC LT Anus2"
	anchor[18] = "NPC LT Anus1"
	bone[19] = "NPC RT Anus2"
	anchor[19] = "NPC RT Anus1"
	bone[20] = "NPC LB Anus2"
	anchor[20] = "NPC LB Anus1"
	bone[21] = "NPC RB Anus2"
	anchor[21] = "NPC RB Anus1"
	k = 0
	while k < 22
		names[n] = "d_" + bone[k]
		nodeA[n] = herNodes.Find(bone[k])
		nodeB[n] = herNodes.Find(anchor[k])
		n += 1
		k += 1
	endwhile
	; the penis tip to her openings
	int tip = -1
	if him
		tip = FindNode(himNodes, "Penis_05_SMP", "Penis_05")
	endif
	String[] target = new String[6]
	target[0] = "Clitoral1"
	target[1] = "VaginaB1"
	target[2] = "VaginaDF02"
	target[3] = "VaginaDB02"
	target[4] = "VaginaDeep1"
	target[5] = "NPC LT Anus2"
	k = 0
	while k < 6
		names[n] = "tip_" + target[k]
		nodeA[n] = tip
		nodeB[n] = herNodes.Find(target[k])
		onHim[n] = true
		n += 1
		k += 1
	endwhile

	String header = "t"
	k = 0
	while k < n
		header += "," + names[k]
		k += 1
	endwhile
	String run = "run " + (started as int)
	GardenOfEden2.WriteIni(LogFile(), run, "her", "form " + her.GetFormID())
	if him
		GardenOfEden2.WriteIni(LogFile(), run, "him", "form " + him.GetFormID() + " tip node " + himNodes[tip])
	else
		GardenOfEden2.WriteIni(LogFile(), run, "him", "none (no actor with A-Body penis bones)")
	endif
	GardenOfEden2.WriteIni(LogFile(), run, "header", header)
	Debug.Notification("SoftBody log: logging")

	int sample = 0
	float t0 = Utility.GetCurrentRealTime()
	while FourStim.IsInScene(her) && Utility.GetCurrentRealTime() - t0 < afMaxSeconds
		String line = "" + (Utility.GetCurrentRealTime() - t0)
		k = 0
		while k < n
			float d = -1.0
			if nodeA[k] >= 0 && nodeB[k] >= 0
				if onHim[k]
					d = GardenOfEden2.GetNthNodeDistanceFromReference(him, nodeA[k], her, nodeB[k])
				else
					d = GardenOfEden2.GetNthNodeDistanceFromNode(her, nodeA[k], nodeB[k])
				endif
			endif
			line += "," + d
			k += 1
		endwhile
		GardenOfEden2.WriteIni(LogFile(), run, "s" + sample, line)
		sample += 1
		Utility.Wait(0.5)
	endwhile
	GardenOfEden2.WriteIni(LogFile(), run, "samples", "" + sample)
	Debug.Notification("SoftBody log: done, " + sample + " samples")
EndFunction

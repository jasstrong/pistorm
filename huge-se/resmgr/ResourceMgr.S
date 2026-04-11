/* Converted from MPW assembly by mpw2gas.py */
    .text
    .even

//
//	File:		ResourceMgr.a
//
//	Written by:	Bruce Horn			22nov82 to 10sep83
//
//	Copyright:	¬© 1988-1993 by Apple Computer, Inc.  All rights reserved.
//
//	Change History (most recent first):
//
//	  <SM61>	 11/7/93	SAM		Roll in <MC5> from mc900ftjesus.
//	   <MC5>	 11/7/93	SAM		Fixed a couple of calls to GetMap/GetSysMap in vRMgrStdEntry
//									that were checking A4 as the function result, which is wrong and
//									will never indicate failure. Removed a couple of redundant loads
//									of ExpandMemPtr (tons of room for improvement here...).
//	  <SM60>	10/27/93	rab		Backed out change SM45. It fixed the bug, but caused a
//									significant loss of performance (‚âà3x).
//	  <SM59>	 9/16/93	SAM		In RsrcZoneInit, check for the zonePtr = bufPtr before calling
//									_TrashZone.  The boot process calls _InitApplZone w/ApplZone =
//									bufPtr as a bogus zone.  The heap utils assumes the zone passed
//									to it is a zone (duh!).
//	  <SM58>	 9/13/93	SAM		Added a call to _TrashZone (HeapUtils) to the start of
//									RsrcZoneInit.
//	  <SM57>	 6/22/93	kc		Roll in bug fix from Ludwig.
//	  <LW15>	 6/17/93	mal		(kc) Fixed NameUsedByOthers to save A2 then restore into A3.
//	  <SM56>	  6/6/93	BT		Roll in the process manager patch on GetResource under the
//									conditional compile flag 'hasRISCV0ResMgrPatches'.  This is for
//									speed so we don't hit mixed mode so often.  Compatibility with
//									old process manager is maintained, this is patch is an app
//									anachronism.
//	  <SM55>	 5/30/93	SAM		Replaced several _Get/SetZone to MOVE.L A0,TheZone (etc).
//	  <SM54>	 5/27/93	SAM		Reintroduced the "is this resource from the system" check in
//									checkload.  Moved the definition of mapForceSysHeap into
//									ResMgrPriv.a equate file.
//	  <SM53>	 5/21/93	CSS		Remove FixSMgrWorld import per P. Edberg review. Plus change
//									$Axxx to _PrGlue per Cyclone review.
//	  <SM52>	 5/14/93	PN		Take out the check load roll in from ProcessMgr because we are
//									now still using the old ProcessMgr therefore it applies the
//									check load fix twice.
//	  <SM51>	 4/28/93	CSS		Fix Radar bugs #1079158. RmveResource had several problems: It
//									was not reporting errors correctly and the check for the sysmap
//									against the curmap should have been for the system override map.
//									Also, in FlushResourceCache clear the expandmem
//									emLastMapOverridden before the check if emResourceCache is clear
//									because sometimes these two aren't set up together. If we call
//									FlushResourceCache, emLastMapOverriden must be cleared.
//	  <SM50>	 4/26/93	CSS		Fix comment for <SM49>.
//	  <SM49>	 4/26/93	CSS		UseResFile doesn't work right with overrides. Because it calls
//									StdTwoExit (which restores the curmap if overrides are enabled).
//									Fix UseResFile to clear local override flag so that we don't write
//									over curmap after we just changed it.
//	  <SM48>	 4/16/93	kc		Add AddNewRefWithoutUpdate entrypoint to AddNewRef so that
//									MakeOverrideMap doesn't set the mapChanged bit. Add code to cach
//									the override map refnum so that resedit doesn't corrupt the
//									system file. This fixes #1069183.
//	  <SM47>	  4/9/93	CSS		Change scratch register from D0 to A0 because Eric A. did it
//									that way in the shipping products.
//	  <SM46>	  4/8/93	CSS		Rollin fix to UpdateResFile to allow it to work correctly in
//									multithreaded environment. (from Eric Anderson).  Radar bug
//									#1077394.
//	  <SM45>	 3/24/93	CSS		Try _FlushFile after SetRFEof calls to hopefully fix the various
//									-39 bugs (eof errors). Add the utility RFFlush which will
//									acomplish the flush. Changed a short branch to word branch.
//	  <SM44>	 3/22/93	CSS		Code to restrict the Uniqueid fuction to 128-32767 was checking
//									against d0 which is how it was in the patch, but in this context
//									is incorrect. In the patch d0 was pulled off the stack. In the
//									current code the candidate unique id is in d2. So, I changed
//									the code to reflect this. This fixes Radar bug #1063282, and
//									#1062580.
//	  <SM43>	 3/19/93	CSS		In LoadResource, clear ResErr if the resource is in ROM so
//									Hypercard doesn't blow its brains out when in sees lint in
//									ResErr after trying to load a WDEF (which is in ROM).
//	  <SM42>	 3/16/93	GMR		Fixed the PRAM combo validity check- changed BLE.S to BLS.S to
//									properly handle $80-$FF values.
//	  <SM41>	  3/2/93	PN		The rGetResource patch is only for a specific case when it
//									looking for 'mitq' resource and it should not be rolled into
//									ROM. So I back out the change.
//	  <SM40>	  3/2/93	PN		Fix the previous change to set current map before calling
//									GetResource according to DY.
//	  <SM39>	 2/26/93	PN		Roll in a patch StartRGetResourceSearchFromTopOfResourceChain to
//									rGetResource
//	  <SM38>	 2/26/93	BT		Change call in GetIndResource to call CheckLoad, not jump
//									through it's vector. This breaks FlashPort and doesn't buy
//									anything.
//	  <SM37>	  2/5/93	PN		Roll in a_checkload proc from ProcessMgrMisc.a into CheckLoad
//	  <SM36>	 1/28/93	PN		Fix comment to call PrGlue
//	  <SM35>	 1/11/93	PN		Fix Radar# 1059658. The AppleTalk ImageWriter and LQ AppleTalk
//									ImageWriter are not 32bit clean. They set the high bit in master
//									pointer block to lock the handle. Until that bug is fixed in the
//									drivers. This code is used to fixed that problem and is put into
//									OpenResFile.
//	  <SM34>	12/16/92	PN		Fix LoadResource to handle NILHandle correctly
//	  <SM33>	12/10/92	PN		Radar# 1051364 Fix LoadResource to check for purged resource and
//									nil handle before doing HandleScan.
//	  <SM32>	 12/4/92	RB		In DetachResource and ReleaseResource, do not dispose of the
//									handle to resources that are in ROM in the Override Map used for
//									system overrides.
//	  <SM31>	 12/4/92	SWC		Moved InitRSRCMgr here from StartInit.a.
//	  <SM30>	 12/1/92	RB		Removed the GetResource hack since we have figured out a better
//									way.
//	  <SM29>	11/20/92	RB		Fixed a scrw-up in the last chekin, it'll the code will not
//									really do what I wanted.
//	  <SM28>	11/20/92	RB		In GetResource, if the type is WDEF or MDEF try looking in ROM
//									first, because the Process Manager is not looking in ROM. I know
//									this is gross, but it's not like it has not been done before.
//	  <SM27>	11/11/92	BT		Add dummy entry points that FlashPort will use to attach hints
//									to. Use of routine entry point was not sufficient to maintain
//									offset integrity when source changes move code that is being
//									hinted.
//	  <SM26>	11/10/92	RB		Renamed CheckGrow@1 to CheckGrowAt1, since Flashport don't like
//									it.
//	  <SM25>	10/22/92	CSS		Change some branch short instructions to word branches.
//	  <SM24>	 9/28/92	PN		Take out the unneccessary save A0 and A1 around GetOverrideMap
//									calls
//	  <SM23>	 9/25/92	PN		#1042784 Fix AddResource and UpdateResFile to flush the
//									ResourceCache and also set the mapChanged bit in the map that is
//									overridden to ensure that the override map is updated when the
//									original resource map is updated.
//	  <SM22>	 9/17/92	kc		Change Bsr to BigJsr.
//	  <SM21>	  9/9/92	kc		Fix bug in SizeResource that was corrupting TeachText.
//	  <SM20>	  9/8/92	PN		Radar#1041949. Fixed the problem in UpdateResFile where it
//									should return ResFNotFound when an invalid refnum being passed
//									in.
//	  <SM19>	 8/28/92	kc		Fix bug in SizeRsrc.
//	  <SM18>	 8/12/92	PN		Temporary fix the LoadResource call so that if a handle of a
//									resource in ROM is passed in it will do nothing, otherwise it
//									will load the resource as always.
//	  <SM17>	  8/6/92	PN		FM. Fixed the last check in so that it does not destroy chooser
//									resource. It is working now, more work is still needed.
//	  <SM16>	  8/6/92	FM		Fixed bug in UpdateResFile. Needed to turn off resource
//									overrides when calliing  stdentry. Also need to set up a2/a3
//									when calling updateresfilecommon.
//	  <SM15>	 7/27/92	FM		Fix mistake in AddResource, where I wasn't preserving
//									emScanOverrides correctly!
//	   <SM14>	 7/27/92	FM		Roll in the ResourceOverrides code from reality.
//									Here are the many changes I have made to the resource manager:
//
//									-	Fixed LoadResource. Call HandleScan BEFORE checking for purged handle or else NIL and non-resource
//										handles can mess us up!‚Ä¶
//
//									-	put installproc to turn on resource overrides in InitResources(). Makes more sense here
//										than in the startinit time stuff‚Ä¶
//
//									-	Changed permission byte and DirID handling for the OpenResFile suite of calls
//										(HOpenResFile, OpenResFile, OpenRMgrPerm and CreateResFile as well). The old-style default values are
//										set up in StdEntry. HFS aware calls then fill in the ioBlock in the Resource
//										Managers stack frame as needed.
//										This should fix some of the QuickTime 1.1 problems we were having‚Ä¶
//
//									-	Removed the DirID being passed into NewMap.
//
//									-	Made HOpenResFile, OpenRFPerm and OpenResFile share common code in
//										OpResFile common.
//
//									-	Added SetFileIODirID as a companion to SetFileIO since ioPosOffset and the ioDirID
//										collide in a paramblock.
//
//									-	Changed NewMap to clear overrideBits when creating NewMaps, in case they
//										were written out to disk.
//
//									-	Optimized the code to get often used OverrideSysMapHndl from the ResourceMgr
//										stack instead of calling GetOverrideMap.The stack is set up in StdEntry.
//										RsrcZoneInit now does this for example.
//
//									-	RsrcZoneInit no longer checks the systemmHeapBit when determining which resources to
//										purge. Instead in checks the location of the resource handle to determine if it should
//										be in the system heap or not.
//
//									-	Changed CloseAllFiles to use the OverrideSyMapHndl stack value instead
//										of calling GetOverrideMap.
//
//									-	Got rid of ORFCommonHFS entry point since it is no longer needed.
//										DirID is now set up on entry to an openrefile call. Now use OverrideSysMap to
//										get string $E000 when checking for opens to the .Print driver. That way overrides
//										will work correctly for this string resource.
//
//									-	Special cased UseResFile when the user passes in 0. This is because unlike all other calls
//										using resource overrides, we want UseResFile to set to the SystemMap in this case and not
//										override of the SystemMap which is what StdMapEntry would do.  (Actually UseResFile has
//										been defined to not respect override maps in any case, not just the case of the system map.)
//										This makes some sense since when we set CurMap to equal SysMap, most ResourceMgr calls
//										will end up using the System Override map anyways. This is because StdEntry will set up
//										CurMap to be the override map on entry.
//
//
//									-	Made lots of changes to UpdateResFile to reflect the use of override maps.
//
//									-	Check to see if we‚Äôre updating the system file, or just another file.  If we‚Äôre updating the
//										system file, then make sure we update all the font files too.  If it‚Äôs just a generic resource map,
//										update just it‚Äôs override maps and the file itself.
//
//									-	from DTY->
//										Note:  I know that explicitly updating an override map of the system won‚Äôt update the
//										font files.  I think this is the right behaviour, since anyone that is referencing
//										the override map explicitly probably has something specific in mind.
//
//									-	Changed CloseResFile to respect the override bit preventFileFromBeingClosedBit. Also
//										added a call to CheckAndFlushFontCache which will flush the font cache if we are closing
//										a file that contains 'FOND's or 'FONT's.
//
//
//									-	Completely rewrote Count(1)Resources to use Deans new Resource caching code. His caching
//										code is smart enough to predict that someone doing a CountResources will soon be doing
//										a GetIndResource. Since the results of the CountResources are cached, the GetIndResource
//										calls should be much faster.p
//
//
//									-	Completely rewrote GetIndResource to use the new caching code. I'm not sure that
//										the RMgrPerm -> ioBlock+ioPermssn(a6) is necessary but thats how Dean wrote it. I'll
//										check it out with him tomorrow.
//
//									-	Changed Get(Cur)Map to use the OverrideSysMap instead of SysMap.
//
//									-	Changed NextMap to work correctly with override maps in the OneDeep case. It used to
//										be that NextMap would bail when ResOneDeep was set. Now if the map has the overrideNextMap
//										or twoDeepBit set then we try the next map even in OneDeep mode.
//
//									-	Reworked StdEntry to iomplement ResourceOverrides and HFS Aware ResourceMgr calls.
//										StdEntry now seth the ioBlock to its default values. It sets up CurMap to reflect
//										any override maps that exist for the old CurMap. (CurMap is restored to its previous value
//										in StdExit.) I also save of the OverrideSysMapHndl and OverrideSysMap since they are
//										so frequently used. After all of this is done if RomMapInsert is true StdEntry calls
//										the rewritten SwapRomMap.
//
//									-	Rewrote SwapRomMap to respect the SystemOverrideMap.
//										Now that we have overrides, we want to put the ROM map at the
//										very end of the resource chain when swapping it out
//										and we want to put it in front of any system override maps
//
//									-	Rewrote StdExit to restore all the changes necessary to make resource overrides work.
//
//									-	Added code to CheckLoad to set the DeanBit when loading a resource and to clear the
//										Dean bit if the handle already exists. That way people can tell if they caused a resource
//										to be loaded or not.
//
//									-	Added a new utility routine called UpdateOverriddenChangedBits. This marks the changed bits
//										for any files overriden by the map in A4. This is called anywhere that the ResourceMgr
//										sets MCCMask or mapChanged on a map.
//
//									-	Rewrote FlushChangedFonds to be register based because it is more efficient
//										and because it allows us to easily call it after StdEntry which fits better with the
//										existing architecture(?) of the ResourceMgr.
//
//									-	WriteResource now flushes the resource cache.
//
//									-	rolled in Phillips fix for DiskCachePurge
//											  <SM13>	 7/10/92	PN		Fix a bug in RPHook that cause the call to crash on rts. Put the
//																			handle on the stack again before jsr DiskCachPurgeProc
//
//
//									-	In AddResource Got rid of FlushChangedFONDsForAdd. I call the new register
//										based FlushChangedFonds now.
//
//										Also change AddResource to turn off emScanOverrides before calling StdZEntry.
//										This is because AddResource is one of the few cases where we actually
//										don't want to use an override map. (UseResFile is another example!)
//
//									-	RmveResource is documented to only allow you to remove a resource
//										when it's homeresfile is in CurMap. With overrides we want to modify
//										this behavior slightly to allow RmveResource calls from CurMap
//										and any files that are overridden by CurMap.
//
//										We also special case to allow Rmve's from system files that are below the system when
//										the CurMap == SysMap. (e.g. Font Files)
//
//									New Routines Added--->
//
//									-	New routine CacheResource from ResourceMgrExtensions.a. Made a few changes to
//										work outside of its previous patch environment.
//
//									-	IsDuplicate - utility routine called by CacheResource. Determines whether a resource
//										and ID have already been cached or not. This is how we prevent duplicates from appearing
//										for maps with the dontCountOrIndexDuplicates bit.
//
//									-	DoesCachedMapOverrideCurrentMap - utility routine called by CacheResource. Given two
//										resource map handles, this routine determines if one is an override map of the other.
//										This is done by traversing the resource chain from the second resource map until a map
//										is found that does not have the overrideNextMap bit set.  If any of the resource maps
//										that are visited during the traversal match the first resource map parameter, this routine
//										returns that the current map is overriden by the cached map.
//
//									-	CheckAndFlushFontCache - Called By:	OpenResFile, HOpenResFile, OpenRFPerm, CloseResFile.
//										If the map being opened or closed contains 'FONT' or 'FOND' resource we invalidate
//										the font caches.
//
//									-	FindOverriddenMap- Given a handle to a resource override map, find the map it overrides.
//										This routine assumes that the map passed in is an override map.
//
//									-	FlushResourceCache - disposes the resource cache handle to force the cache to
//										be rebuilt next time _CountResources or _GetIndResource is called.  The cache
//										will be flushed whenever the offset to a resource stored in the cache may become
//										invalid (when resources are added or removed from the map or when the map is updated or
//										written to disk) and when files are opened or closed which may cause the count to
//										become invalid.
//
//
//	  <SM13>	 7/10/92	PN		Fix a bug in RPHook that cause the call to crash on rts. Put the
//									handle on the stack again before jsr DiskCachPurgeProc
//	  <SM13>	 6/12/92	CSS		Roll-in Reality Changes:
//									<12>	 6/11/92	FM		Remove obsolete call to FixSMgrWorld.
//	  <SM12>	  6/3/92	PN		Fix the HCreateResFile to get the dirID and VRefNum correctly.
//	  <SM11>	  6/3/92	CSS		Integrate patch to RsrcZoneInit.  Fix to CloseFiles to not close the override files that override
//									the system resources.  This is based on a patch to RsrcZoneInit
//									that changes SysMapHndl to fake out closefiles to effectively do
//									the same thin.
//	  <SM10>	  5/7/92	stb		roll in TrueType patch on SetResPurge for disk-based font caches
//									(from DiskCachePatches.a).  Rewrote to no longer need the
//									TrueType global reference to the resource mgr purge proc,
//									although the TrueType routine is still in place for RAM
//									versions.
//	   <SM9>	 4/24/92	PN		Roll in NewCreate and NewOpenRF ValidatefileSizeInCheckGrow/
//									OpenDirIDResfile/
//									OpenRFPermDirIDExit/OpenresfileDirIDExit/CreateDirIDresfile/Crea
//									teresfileDirIDExit from ResourceMgrExtensions.a
//	   <SM8>	 4/18/92	kc		Fix bug in GetIndResource. (Change TST.L to TST.W)
//	   <SM7>	 4/14/92	stb		Add the comments I so conveniently forgot to add‚Ä¶
//	   <SM6>	 4/14/92	stb		remove the ‚Äòscod‚Äô patch on GetResource. Roll in decompression
//									patches.
//		 <5>	 3/11/92	stb		fix GetIndResource and Get1IxResource so the result gets cleared
//									BEFORE bailing out if the index was bogus.  Flush 'FOND' candidate
//									lists in AddResource, RmveResource, and ChangedResource.
//									unroll <10> 'scod' patch on GetResource.  Whoops, not yet.
//		 <4>	 2/26/92	stb		roll in patches for LoadResource, AddResource, GetIndResource,
//									UniqueID. Fix ROZ map handle size bug in CountCombos where
//									unnamed resources caused 255 bytes extra to be allocated in the
//									map handle. Removed obsolete (conditionalized out) code in
//									ORFCommon.
//		<12>	 2/14/92	stb		remove support for ignored resources (conditional: hasResIgnored)
//	    <11>	 2/14/92	stb		roll in fix to RmveName so that a name doesn‚Äôt get removed if it
//									is in use by any other resource.  This seems odd, but files like
//									this exist, and we want them to work.
//		<10>	 1/20/92	PN		Roll in the linkedpatch from segmentloaderpatches.a to force
//									loading 'scod' resource
//		 <9>	  1/8/92	RB		Exported a few symbols so that the LinkedPatches for
//									ResourceMgrExtensions can be used from ROM.
//		 <8>	 6/12/91	LN		changed #include 'HardwareEqu.a' to 'HardwarePrivateEqu.a'
//		 <7>	 9/13/90	BG		The unnamed author referred to in <6> is helping simplify things
//									by removing code added for -eclipseDebug- because it is no
//									longer necessary with the latest rev. of the 040. In general,
//									mods <4> and <5> were removed.
//		 <6>	 9/12/90	MSH		&SWC: Fixed a bug in some eclipseDebug code in NameResource. The
//									code was replacing an ADDA.W offset(Ax),Ax instruction with
//									several instructions. However, the author of the replacement
//									code didn't realize that that particular ADDA also does a sign
//									extend of what's at offset(Ax) before, doing the add, and his
//									code didn't. Very messy if the addition happens to occur with
//									particular values.
//		 <5>	  8/6/90	BG		Some more EclipseNOPs were needed.
//		 <4>	 7/17/90	BG		Added EclipseNOPs for flakey 040s.
//		 <3>	 6/27/90	CCH		Fixed bug in CountCombos that increased size of ROM rsrc map by
//									255 bytes for each ROM rsrc without a name. Also removed code
//									for non-combo ROM resources.
//		 <2>	  2/1/90	CV		Checking for an FPU based on the combo and HWCfgFlags. If the
//									FPU is present, the combo is changed to 3 which uses the FPU
//									SANE package rather than the non-FPU SANE.
//	   <1.6>	 11/8/89	BPA		Added support for ignored resources (conditional: hasResIgnored)
//	   <1.5>	 10/2/89	BPA		Catenated source files RmgrAsm1.a, RmgrAsm2.a and RmgrAsm3.a
//									source files to this file.
//	   <1.4>	 8/30/89	dba		moved common equates for ResourceMgrPriv.a
//	   <1.3>	 6/30/89	GGD		NEEDED FOR AURORA: Added include of HardwareEqu.a,
//									UniversalEqu.a, deleted Nequ.d
//	   <1.2>	 6/29/89	BBM		Added vectors for vRMgrStdEntry and vRMgrStdExit in RMgrAsm2.a
//	   <1.1>	11/10/88	CCH		Fixed Header.
//	   <1.0>	 11/9/88	CCH		Adding to EASE.
//	  <‚Ä¢1.1>	 9/23/88	CCH		Got rid of inc.sum.d and empty nFiles
//	   <1.0>	 2/11/88	BBM		Adding file for the first time into EASE‚Ä¶
//______ EASE Modification History for file RmgrAsm1.a ____________________________________
//	   <1.9>	 8/30/89	dba		changed MapReadError to mapReadErr
//	   <1.8>	  7/6/89	PKE		NEEDED FOR AURORA & ESPRIT: Fixed Script Mgr initialization in
//									InitResources to work properly for switch launch.
//	   <1.7>	 6/30/89	CCH		Modified RedoMap to use a new field in ROM rsrc entries that
//									points to rsrc data.
//	   <1.6>	 6/30/89	GGD		Added default combo selection when PRAM combo is 0, or out of
//									range.
//	   <1.5>	 6/15/89	BBM		Added support in InitResources for combinational resource in
//									ROM. The ROM resource map is now generated at bootime depending
//									on the combination byte stored in Pram.
//	   <1.4>	 6/12/89	dba		Changed the logic that checks for old printing glue opening the
//									printer file.
//	   <1.3>	  3/3/89	CCH		Now checks IntlSpec to see if Smgr has been initialized instead
//									of ExpandMem.
//	   <1.2>	 2/17/89	CSL		Clean up PartRead and UpdateResFile for 32 bit clean.
//	   <1.1>	11/10/88	CCH		Fixed Header.
//	   <1.0>	 11/9/88	CCH		Adding to EASE.
//	   <1.2>	10/27/88	PKE		added script manager support to initresource
//	   <1.1>	 8/26/88	BBM		Example to show how to use ease.
//	   <1.0>	 2/11/88	BBM		Adding file for the first time into EASE‚Ä¶
//______ EASE Modification History for file RmgrAsm2.a ____________________________________
//	   <1.3>	 8/30/89	dba		changed MapReadError to mapReadErr
//	   <1.2>	 6/29/89	BBM		Added vectors for StdEntry and StdExit
//	   <1.1>	11/10/88	CCH		Fixed Header.
//	   <1.0>	 11/9/88	CCH		Adding to EASE.
//	   <1.0>	 2/11/88	BBM		Adding file for the first time into EASE‚Ä¶
//______ EASE Modification History for file RmgrAsm3.a ____________________________________
//	   <1.2>	 8/30/89	dba		changed ResErrAttr to resAttrErr
//	   <1.1>	11/10/88	CCH		Fixed Header.
//	   <1.0>	 11/9/88	CCH		Adding to EASE.
//	   <1.0>	 2/11/88	BBM		Adding file for the first time into EASE‚Ä¶
//
//	To Do:
//


//_________________________________________________________________________________________
//
// Set all undefined conditionals and equates to default state
//_________________________________________________________________________________________

//_________________________________________________________________________________________
//
// Macintosh Resource Manager
//
// Written by Bruce Horn			22nov82 to 10sep83
// Modified by Brian B. McGhie	01jan85 to Present
// Modified by Bryan Atsatt		02oct89 to Present
//
// This file contains the source code for the Resource Manager.  It includes files
// which contain toolbox, graftype and sysmacs equates and macro definitions.
//
// As with the OS routines, the Resource Manager routines, with one exception,
// smash A0 and D0 only;	all other registers are preserved. LoadResource
// preserves ALL registers.
//
// The Mark menu lists most of the routine entry points;  names preceded by an apple
// are external entries (A-Traps), those marked with a diamond are internal entries.
// All external entry points are listed in the Exports section (use Mark menu).
//
// This file is built with the following conditionals:
//
//   hasComboResource: 	true	(will always be true!)
//
//_________________________________________________________________________________________
// EJECT 
//_________________________________________________________________________________________
//
// Revision History
//_________________________________________________________________________________________
//
//		16-Dec-82		BLH 	Added Add/RmveResource.  Massive name changing.
//		21-Jan-83		BLH 	Updated to new LK file system.
//		11-Feb-83		BLH 	Returned error codes in InitResources,
//								OpenResource
//		17-Feb-83		BLH 	Fixed bug in CheckLoad
//		25-Apr-83		BLH 	Rom 3.0 overhaul:
//									  fixed bug in UpdateResFile, optimizing
//									the writes and making it a little quicker
//									to call updates.
//									  Changed openResFile to use the resource
//									fork, and to handle more reasonably the
//									already-open, not-open, and other error
//									conditions.
//									  CloseResFile now releases all resources
//									in the map before closing, compacts the
//									resources in the map, and writes out the
//									possibly modified map and header.
//									(@@Note--CloseResFile does not compact
//									   yet.  Due to a bug in GetHandleSize,
//									   this feature has been disabled. )
//									  The resource file starts at ZERO instead
//									of 512.
//									  Added memory full error handling in
//									OpenResFile and CheckLoad.
//									  Added preLoad capability to initResources
//									and OpenResource.
//									  Added procedure WriteResource.
//									  Added ResAdded resource attribute.
//									  Added MAttr, map attributes word, in map.
//									  MapSize and DataSize are now LONGS.
//									  Resource manager routines now save
//									ALL registers except A0 and D0.
//									  Added SetResPurge to handle the purge
//									hook in the Memory Manager.
//									  Added CurResFile.
//	12-May-83  AJH	 fixed bug in InitResources -- shouldn't preload when
//					 you can't read the map
//	24-May-83  BLH	 Fixed bug in NewMap--crashes if failed due to unlink bug.
//	25-May-83  BLH	 Changed interface to CmpString in GetNamedResource.
//	31-May-83  BLH	 Added CreateResFile, fixed AddResource bugs.
//	 1-Jun-83  BLH	 Added DetachResource.
//	 6-Jun-83  BLH	 Fixed bug in GetNamedResource (didn't search all maps)
//	 8-Jun-83  BLH	 Fixed bug in CloseResource (!)
//	10-Jun-83  BLH	 Moved compacting and mapwrite from CloseResFile to Update.
//					 Added routine to close maps in .
//					 CloseResFile closes all maps if you attempt to close
//					 Sys.rsrc.  UpdateResFile now compacts, since GetHandleSize
//					 finally works.
//	 13-Jun-83 BLH	 Made SetResAttr, SetResInfo NOT set the mapChanged bit.
//					 you have to explicitly call ChangedResData to make sure
//					 the change is written out to the map.
//	 18-Jul-83 BLH	 Added MapCompact attribute bit to minimize resource file
//					 compacting.  Added UniqueID to return a unique ID for a given
//					 type in the current map.  Also, shrunk code approximately
//					 60 bytes due to suggestions from Donn Denman.
//	 8-Aug-83  BLH	 Removed BeginSubResource, EndSubResource. Added HomeResFile.
//					 Added ResErrProc in globals.  Made UniqueID return an
//					 ID which is unique in all open maps.  Took out SetEOF
//					 from UpdateResFile, to allow pre-allocation of the
//					 resource fork.  Put in ResrvMem to reserve low locations for
//					 locked handles.  Added file-order preload and update to
//					 speed up resource I/O.
//	12-Aug-83  BLH	 Split Resource Manager into four files:  RMgr, RMgrAsm1,
//					 RMgrAsm2, and RMgrAsm3.  Utilities in RMgrAsm2.  Added
//					 CheckGrow to handle the DiskFull condition.  CheckGrow
//					 called from ChangedResource, AddResource, and AddReference.
//					 Also Added ReallocRes, AllocRes, called from (surprise!)
//					 changedResource and AddResource to allocate space for
//					 either a new resource or a resource which has changed
//					 size and needs to be reallocated at the end.
//	16-Aug-83  BLH	 Bug fixes and space-savers.
//	29-Aug-83  BLH	 Fixed bug in SetResInfo, RmveResource (protect bit checked
//					 in RHndl instead of RAttr) and made CheckGrow handle
//					 memoryFull condition as well as diskFull.
//					 AddResource/AddReference now return ResErr=DiskFull or
//					 MemFull in the case that CheckGrow failed.
//	 1-Sep-83  BLH	 InitResources shouldn't clear ResErrProc.
//					 Fixed NOP (branch to next instr!), folded cmn code in
//					 name stuff.  Fixed dangling ptr problem (!) caused by
//					 CheckGrow.  Took out dangerous LINK stuff in SetFileIO
//					 and just used a standard stackframe on first link in
//					 StdEntry.	Made GetCurMap and CloseResFile handle the case
//					 of no maps open (set curMap to FFxx with ST).	Fixed bug
//					 in LoadResource where errors would not get reported through
//					 the ResErrProc.  AddResource sets the Resource bit in the
//					 masterPtr, RmveResource and DetachResource clear it.
//	 2-Sep-83 BLH	 MAJOR CHANGE:	threw away name ID stuff, made hard offsets
//					 to the names in the name block stored in the resource entry
//					 where the nameID was previously stored.  This change
//					 fixed a time-bomb bug which would trash the resource file
//					 after repeated SetResInfo calls.  Also, data lengths in
//					 the resource file are now LONG integers (4 bytes).  Fixed
//					 CountTypes, GetIndType to return true counts and unique types
//					 for all open maps, to be consistent with CountResources
//					 and GetIndResource.
//	  6-Sep-83 BLH	 Added MapReadOnly in MAttr to inhibit writing to the resource
//					 file and diskFull checking.  Added SetResFileAttrs,
//					 GetResFileAttrs to allow toggling of this bit.
//	  8-Sep-83 BLH	 Fixed bug in CheckGrow which could trash the resource map
//					 if the handle passed to ChangedResource or AddResource
//					 was purged.
//	  9-Sep-83 BLH	 Fixed bug in CheckLoad--ResrvMem must preserve A0, so typed
//					 in .Word $A040 to replace _ResrvMem.  Fixed bug in RmveName.
//	 10-Sep-83 BLH	 Fixed bug having to do with protected resources; cleaned
//					 up usage in UpdateResFile, ChangedResource, WriteResource.
//_________________________________________________________________________________________
// Version 7.0. Last modified 10-Sep-83 by Bruce Horn.  Later changes by BBM.
// All changes from this point on are part of the development of the BigROM.
//_________________________________________________________________________________________
// 22jan85		BBM started to roll in patches to the rom source.
// 29jan85		BBM Cleaned up a lot of internal tables and comments.
// 02feb85		BBM Changed InitResources to support ROM Resources.  Since We
//				need some of the ROM Resources (e.g. the sony drvr) at boot time
//				before we have mounted (or can mount) the disk, we now call
//				InitResources twice.  The first time it enables just the ROM
//				resources, moving both the map and the master pointers down from
//				the ROM.  The Second time, it sets up the system resource map from
//				off of the disk as the root resource map, and moves various ptrs
//				so that the system file is the root map and the ROM map is next
//				in line down to ram.  This enables people to lock down ROM
//				resources without causeing a bus fight, Since the map and pointers
//				used are not in ROM.  Initresources calls BuildMap, which is new.
// 11feb85		BBM Changed CloseFiles and CloseResFile so they couldn't close ROM
//				resource map.  This needs to be modified later, as there should be
//				a way to override ROM resources.
// 12feb85		BBM RsrcZoneInit now won't try to release rom resources.  Also
//				changed ReleaseResource so it wouldn't release ROM resources.
// 13feb85		BBM changed the format for the ROM resources associated code had
//				to be changed. (see InitResources and BuildMap)
// 18feb85		BBM The ROM resources now work right on switch-lanching.  As we
//				are now using the new system equate files for the build rom
//				proceedure, I had to change one equate (from resource to resourc).
// 27feb85		BBM If you call CreateResFile on a locked disk, an incorrect error
//				code was being returned.  Bug was that after _Create, no check for
//				error was done.  Fix was to stuff error code into ResErr and rtn.
// 28feb85		BBM CurResFile now returns zero if the current res file is the
//				system resource file, else it returns the actual refnum.
// 03mar85		BBM CloseResfile was wasting time calling DisposeResource when it
//				should call DisposHandle directly. (see note in routine's header)
// 05mar85		BBM changed BuildMap to create a read-only zone for the ROM Rsrcs.
// 07mar85		BBM Added Preload3, which is executed if ResLoad is false (in Pre-
//				Load), in which case there is no need to access the disk.  This
//				avoids the n-squared search on the curmap for preloadable rsrcs.
// 08mar85		BBM The other problem with Preload was that it was checking
//				ResPreLoad at the highest level of the loop.  Added two utilities,
//				FirstR2 and NextR2, which do the duplicate FirstRes and NextRes
//				except they check the preload bit as they scan through the map.
// 09mar85		BBM Fixed CheckLoad.  Now it only checks the size once when it is
//				loading in a resource, instead of checking the size twice.
// 12mar85		BBM added a ram cache for HandleScan.  Involved new low memory
//				variables and modifying routines that would invalidate cache.
//				Added SaveCache routine.
// 13mar85		BBM fixed a bug in release resource where it was trying to release
//				the ROM resources.	Fixed a stack problem in FNInit.
// 14mar85		BBM fixed preload to zero all handles of the new map, as the map
//				read in may have bogus info in those fields.
// 15mar85		BBM fixed 32k limit on the size of resources.
// 19mar85		BBM SaveCache should only save words for D4-D6.  FNInit should pop
//				stuff off the stack using ADDQ #4,SP.  Preload/2/3 should check to
//				see if any rsrcs exist first.  StdExit now zeros A0 for Duvall.
// 22mar85		BBM InitResources/Buildmap calculates new handles for ROMresources
//				but didn't mask off high byte of the masterpointer (which could be
//				locked, etc.).
// 22mar85		LAK&BAS PreLoad3 only created handles for resources that WEREN'T
//				preloaded.	Also, _NewEmptyHandle caused the map to move and A2
//				was pointing into it, so A2 was changed to be an offset into the
//				map around the _NewEmptyHandle call.
// 23mar85		BBM Calling GetMap/GetCurMap with an invalid refnum passed back an
//				error but left most regs setup pointing to the system map.	The old
//				finder blindly used these regs without checking the error CC.  So
//				to be compatible in the new ROM we make the curmap, topmap.
// 25mar85		BBM the fix on 22-mar-85 only preserved A2.  A3 must also live.
// 28mar85		BBM In order to access the ROM Resources nearly all the time, we
//				now set the low memory variable SysMap = 1 (the ROM map refnum).
//				This has implications to a majority of the RMGR routines.  RMGR
//				routines that check rsrc ResProtected bit or check MapReadOnly bit
//				will not need added code to protect them (eg. SetResInfo)
// 29mar85		BBM Initresources(BuildMap) was hard wired to $1C which currently is
//				always in MTypes.  Rather than make life hard in the future, redid
//				code to add in the value in MTypes and not use $1C.
// 02apr85		BBM fixed HomeResFile to return correct RefNum.
// 03apr85		BBM Finished work of 29-mar-85.  DetachResource and
//				ReleaseResource now set ResErr (ResChngBit = -198) if ResChanged
//				bit is set.  AddResource and AddReference would not allow you to
//				add to the system map.
// 09apr85		BBM made FirstR2/NextR2 more general.  These routines now use D1
//				to determine which bit of RAttr to test on the lowest level.  This
//				fix enables both UpdateResource and Preload to use FirstR2/NextR2.
// 10apr85		BBM Added a fix that was from Bud Tribble to ORFCommon.  This is a
//				fix for some print drivers.  Fixed GetResInfo so that if a pointer
//				is passed which is nill, it doesn't attempt to write out that var.
//				Added new routine DoRomEntry to recover handles in the ROMZone,
//				also had to save off ROMZone when created and made ReleaseResource
//				work in the RomZone
// 11apr85		BBM Change DoRomEntry to support a new interface with the Memory
//				manager.  Added routine CheckMap, which does some minimal error
//				checking on rsrc maps.
// 16apr85		BBM GetResource and GetNamedResource now return an error if they
//				couldn't find the resource.  _NewHandle in routine NewMap didn't
//				set ResErr if it returned an error, now it does.  UpdateResFile
//				didn't set ResErr if MapReadOnly is set, now it does.  GetIndResource
//				sets ResErr if input index is zero or negative.  ChangedResource
//				and SetResInfo now return error condition if ResProtected is set.
//				WriteResource now sets ResErr if Map is ReadOnly or Res not changed.
//				CloseResFile returns error code if UpdateResFile fails as well.
// 18apr85		BBM now Preload, if ResLoad is false, doesn't call NewEmptyHandle.
//				SetResPurge now points to the correct Zone.
// 22apr85		BBM Finished the one map deep calls.  Added Seven new entry points:
//				Get1Resource, Get1IndResource, Get1NamedResource, Get1IndType,
//				Count1Types, Count1Resources, Unique1ID.  (added Std1Entry).
// 23apr85		BBM Fixed the checks in GetResInfo which see if the pointers to
//				var parameters are nill.  Started to remove references from RMGR.
//				Also fixed the switcher, to keep all RMGR files open & circumvent
//				the RMGR Andy H. did a MOVE.L SysMapHndl,TopMapHndl and called
//				RsrcZoneInit.  Most likely he is not the only one doing it, so we
//				added an error check in closefiles, which corrects TopMapHndl if
//				it is pointing below the ROMMapHndl.
// 29apr85		BBM Fixed bug in Preload (it would only load in one rsrc).	Fixed
//				bug in CheckMap.
// 30apr85		BBM GetIndResource, GetResource and GetNamedResource all save the
//				HandleScan cache.  Sped up GetIndResource and CountResources by
//				not dealing with system references.
// 01may85		BBM Sped up HandleScan by a whole lot!!!!!	Added a new exit for
//				routines called SubA2Found, to speedup IDScan and TypeScan.  Sped
//				up DoRomEntry and CountITypes.
// 02may85		BBM Added new trap GetMaxSize.	Fixed a bug and (while there) sped
//				up GetIndResource.
// 06may85		BBM Preload could save a word by preload calling GetMap.  Andy H
//				looked at code and suggested some cleanups in SaveCache, SizeRsrc,
//				RdRsrc, RdData, GetHS, HomeResFile, CloseResFile, MoveNames, and
//				added some stuff for the font manager.
// 07may85		BBM Started speed up on Preload, added SuperLoad & GetMaxSize.
// 08may85		BBM fixed a bug in CheckMap (death occured if there were no named
//				resources).
// 09may85		BBM finished the first whack at SuperPreload.
// 10may85		BBM Added new routine GetRsrcCnt which can be used in conjunction
//				with a DBRA D4, instead of GetEntries/NextEntries (forSpeed).
//				Modified FirstR2, NextR2, Preload accordingly.
// 13may85		BBM Sped up FirstRes/NextRes and combined with FirstR2/NextR2 for
//				Space as well as speed.
// 14may85		BBM Fix ORFCommon to use less stack & be a little faster.
// 16may85		BBM made SysMap = RomMap = 2 to fix a bug in Lotus where they were
//				trying to do file IO with the bogus ROM map refnum.
// 17may85		BBM patched out superload, since it was superslow.
// 20may85		BBM fixed bug in SpaceAt to setup D1 before call to NextRes.  Also
//				fixed bug in HandleScan (an off by one bug).  Also made the check
//				in AddResource less strict, to fix a bug in "Printer Installer".
//				Stuffed the MRefNum in the ROM Map with 2, not 1 (see BuildMap).
// 21may85		BBM Backed out change (20may85) to AddResource, in favor of a fix
//				in DoRomEntry.	Fixed SkipToEntry to handle an offset of 64k.
// 22may85		BBM CloseResFile didn't shut down system right.  DetachResource,
//				when passed a nill handle went ahead and munged random memory.	I
//				put the fix into StdResEntry to fix similar problems elsewhere.
// 30may85		BBM made A2/A3 offsets for every IO call. (see WrData2/RDData2).
// 31may85		BBM ReallocRes assumed D1 preserved across SpaceAt.  Fixed same.
// 03jun85		BBM Fixed a bug in IO calls by forcing A4 to be even on entry.
// 07jun85 - 13jun85
//				BBM made a new entry Called 'OddSize', which gets four bytes out
//				of memory and stuffs it into D0.  Finished first massive pass at
//				UpdateResFile.	Eliminated use of one low memory variable.
// 18jun85		BBM GetIndResource sped up and fixed (wrong value in D4 cache).
// 19jun85		BBM fixed bug related to ResOneDeep stuff (in nextmap).
// 20jun85 - 24jun85
//				BBM rewrote superpreload and various stuff to accomodate different
//				loading sequence on preload. (speed improvement).
// 26jun85		BBM made onedeep flag a byte instead of a word.  Remaining byte is
//				used for RomMapInsert.	Note on clearing ResOneDeep we still clear
//				the whole word, as RomMapInsert still needs to be cleared.
// 27jun85		BBM finished pulling ROM Resources.
// 29jun85		BBM _MaxBlock/_NewHandle may move memory (resource map) so need to
//				keep A2 valid in _UpdateResFile.
// 30jun85		BBM optimised RLocnNew.  Fixed CmpFrmDsk.
// 01jul85		BBM fixed UpdateResFile (again?!).
// 02jul85		BBM start code thought that ResErrProc is already setup.  Added new
//				entries into FirstRes/NextRes which zero D1 on entry.
// 03jul85		BBM cleaned up UpdateResFile.
// 08jul85		BBM Fixed some subtle wierd cases in UpdateResFile routines.
// 09jul85		BBM changed lowmem defs, pulled out new updateresfile for now, made
//				powerup code work right with the ROM resource pullout.
// 10jul85		BBM tried a fix in OpenRF in conjunction with the new file system.
//				Fixed a bug swapping rom map in and out, which involved curmap.
//				Finally fixed UpdateResFile - its about as fast as it can be.
// 11jul85		BBM fixed up superload to work with the new ROM insert stuff.
// 15jul85		BBM backed out enhancement to CurResFile (which returned zero if
//				CurMap = SysMap).  F/DA Mover was walking through FCB Queue using
//				zero as a valid refnum.
// 18jul85		BBM added some more error checking on the handlescan cache.  Three
//				calls mess with the map after setting up the cache (AddResource,
//				RmveResource, and SetResInfo) so fixed them as well.
// 19jul85		BBM Sped up RsrcZoneInit.
// 22jul85		BBM Fixed bugs connected with CloseResFile (assumed SysMap <= 2).
//				Removed the MaxSizeRsrc call to make room in ROM.
// 12aug85		BBM Sped up CloseResFile.  Cleaned up most of the special code for
//				the ROM map, and did house cleaning.
// 13aug85		BBM Compacted some code.
// 14aug85		BBM RMGR should work with the map in the front, so fixed SpaceAt,
//				CheckMap and SetRFEOF.
// 20aug85		BBM started a new format for getting address of a rom rsrc.
// 21aug85		BBM fixed and adjusted SizeResource and MaxSizeRsrc, and added new
//				trap called RsrcMapEntry.
// 22aug85		BBM made a standard entry and exit for utility routines called
//				SaveRegs and RestRegs.	Fixed SetResInfo.
// 24aug85		BBM fixed RmveName.
// 26aug85		BBM cleaned up more, Replaced SpaceAt to work with map at front.
//				Added OpenRFPerm.  Removed a lot of SetEOF Calls.
// 27aug85		BBM made massive changes to InitResources and BuildMap.  Added new
//				routines.  This version of RMGR depends on new format of ROM Rsrc's.
// 28aug85		BBM made RomRsrcStart an offset, not hardwired to $40001A.
// 29aug85		BBM fixed two bugs during InitResoruces.
// 04sep85		BBM added routine SizeFits to help in SuperLoad.
// 05sep85		BBM made OpenRFPerm work right.
// 09sep85		BBM fixed bug in AllocRes/ReallocRes.
// 11sep85		BBM fixed bug in MaxRFEOF (wrong if data before and after map).
// 12sep85		BBM fixed bug in updateresfile.
// 14sep85		BBM AddResource inserts rsrc entry at end of rsrc entries for that type.
// 17sep85		BBM Fixed NewMap to check whether map exists if ResFile is open.
// 19sep85		BBM Fixed NewMap to really check if this is a new format preload file.
// 09oct85		BBM fixed UpdateResFile (file's first rsrc needed compaction).
// 10oct85		BBM didn't fix right on 09oct85, but now UpdateResFile is great!
//				(But AllocRes wasn't. forgot to subtract resdataoffset from file loc)
//________________________________________________________________________________________
// BETA ROMS 	LAUREL & HARDY	(all changes from this point are witnessed by two
//				people, the second person in lowercase)
//________________________________________________________________________________________
// 17oct85		BBM/jtc MacWrite 2.2 uses AddReference and RmveReference.  The fix
//				will be to keep the traps but strip the parameters off the stack
//				and return with ResError set as an error.  This makes these two
//				traps equivalent to nop routines.
//				BBM/jtc UpdateResFile could be passed a bad map and get caught in
//				an infinite loop.  Now it cann't get caught in an infinite loop.
// 21oct85		BBM/jtc In UpdateResFile mark ResErr with error if we abort.
// 27oct85		BBM Added some vectors for the low level routines. (True Paranoia!)
//				Changed NewMap to return error if no map found.
// 30oct85		BBM Changed the way that OpenResFile and OpenRFPerm worked with the
//				permission in order to accomidate the segment loader.
//; 04nov85		BBM RmveResource of a ROM resource tried to do SetEOF on ROM RefNum, which is an
//				illegal file call.  This fix skips the eof call if it is the rom refnum.
//				This change is needed if we want ROM Override to work correctly.
// 06nov85		BBM MicroSoft's Word was going behind the font manager's back, which
//				caused the font cache to become invalid.  So I now invalidate the
//				Font Manager's cache if I release a resource of type 'FONT'.
//_________________________________________________________________________________________
// POST LONELY HEIFER FIXES
//_________________________________________________________________________________________
// <06jan86> BBM Took out three lines that shouldn't have been in the routine
//			CmpFrmDsk.	This fixes a bug in UpdateResFile.
// <08jan86> BBM If UpdateResFile cann't get at least 4k for a buffer it dies.
//			Fixed UpdateResFile so it can use a 1k buffer off of the stack.
// <09jan86> BBM cleaned up the way it found space on the stack for UpdateRes.
// <13jan86> BBM Fixed GetMaxLoad to get the right data.  (PreLock, etc., were not
//			addresses, they were offsets.)	This fixes superpreload.
// <19feb86>	BBM Made some modifications to work under MPW
// <13may86> BBM Fixed a bug in updateresfile.  The bug manifested itself when
//			compacting from disk and slideing the resource forward in the buffer
//			from one to three bytes.  This caused the resource size to overwrite
//			itself.  Thus some resources could be several meg long.
//<C169/24Sep86> JTC Changes in all 1,2,3 files for 32-bit support.  Trick change
//				to DoROMEntry presumes that rel handles in mem mgr block headers
//				have only 24 bits, as in the Leung/Fung 32-bit memory manager.
//<C206/09oct86> bbm added support for mpw includes.
//<C238/17oct86> bbm font manager wanted its cache invalidated on opening and
//				closing resource files.  Helps new font manager be compatible with
//				some old applications.
//<C249/20oct86> bbm made the check for a bad map more robust (rmgrasm2(checkmap)).
//<C268/27oct86> bbm one of the macplus patches to updateResFile did not get rolled
//				in.  UpdateResFile now finds buffer space more intelligently.
//<A291/29oct86> bbm memory manager is now long word aligned, which changes ROZinti.
//<A340/03nov86> bbm fixed dumb mistake in patch that was rolled in (see C268).
//<C396/26nov86>	agh	32 Bit address Fixes.  Removed dozens of asm bra size warnings.
//				UNIX 8 byte master pointer mods to ROZInit.
//<C628/14jan87> bbm added rGetResource trap to rmgrasm3.a.  This trap reverses the
// 				order of the system and rom map when doing a GetResource.  (In reality,
//				_GetResource is called with romMapInsert false.  If that fails
//				_GetResource is called with romMapInsert true.)
//<C669/22jan87> bbm fixed rGetResource trap to rmgrasm3.a. (stack cleanup problem)
//_________________________________________________________________________________________
//
// Files Combined
//_________________________________________________________________________________________
//
// <02oct89 bpa>	Combined ResourceMgr.a, RmgrAsm1.a, RmgrAsm2.a & RmgrAsm3.a into one
//				file: ResourceMgr.a.
// <03oct89 bpa>	Modified ZFirstRes/ZNextRes/FirstRes/NextRes so can be called with
//				D1 = resIgnored (0).  Made two copies of Get1IndResource/GetIndResource,
//				one if hasResIgnored and the other if not;  modified the one for
//				hasResIgnored so manages ResIgnored attribute.
// <04oct89 bpa>	Modified PreLoad and UpdateResFile so will clear ResIgnored attribute bits
//				in all entries in map.  Added new routine ClearIgnore.
// <05oct89 bpa>	Added new routine ChkTypeIgnored which checks to see if all entries for a
//				given type are ignored;  modified CountITypes and TypeScan to call it.
//				Modified CountResources/Count1Resources so doesn't count ignored
//				resources.  Modified IDScan so skips any matching ID whose ignore bit is
//				set.
// <06oct89 bpa>	Added new routine TypeScanAll which does not ignore types; modified
//				AddNewRef and RmveResource to call it rather than TypeScan.  Modified
//				IDScan so skips any entries whose ignore bit is set.  Added new routine
//				IDScanAll which does not ignore types; modified UniqueID/Unique1ID to call
//				it (& TypeScanAll) rather than IDScan.  Modified GetResAttrs and
//				SetResAttrs to force ResIgnored bit clear.  Modified GetNamedResource and
//				Get1NamedResource so skips any entries whose ignore bit is set.
// <07nov89 bpa>	Added support for undefined conditionals (sets them to default state).
//_________________________________________________________________________________________

// LOAD 'StandardEqu.d'
// INCLUDE 'HardwarePrivateEqu.a'
// INCLUDE 'UniversalEqu.a' // <1.3>
// INCLUDE 'ResourceMgrPriv.a'
// INCLUDE 'FontPrivate.a' // <sm5>stb<sm10>stb
// INCLUDE 'Decompression.a' // <sm6>stb
// INCLUDE 'InternalOnlyEqu.a' // <PN>
// INCLUDE 'SysPrivateEqu.a' // <sm10>stb
// INCLUDE 'PrintTrapsEqu.a' // <SM56> BT
// STRING asis

//_________________________________________________________________________________________
//
// Export all external entry points
//_________________________________________________________________________________________


    .globl RMgr
RMgr:

    .globl AddResource // Add a resource to the resFile.
    .globl AddReference // a nop routine added back in for MacWrite 2.2	<17oct85> BBM
    .globl ChangedResource // Called to mark resource to be written
    .globl CloseResFile // Given refnum, closes the resource
    .globl Count1Resources // $A81A (26) OneMapDeep version of CountResources
    .globl Count1Types // $A81C (28) OneMapDeep version of CountTypes
    .globl CountResources // Given type, return number of resources
    .globl CountTypes // Returns number of types in current rsrc
    .globl CreateResFile // Given filename, create a null res file
    .globl CurResFile // Returns the current resource file refnum
    .globl DetachResource // Given handle, detaches it from resources.
    .globl Get1IndResource // $A81B (27) OneMapDeep version of GetIndResource
    .globl Get1IndType // $A81D (29) OneMapDeep version of GetIndType
    .globl Get1NamedResource // $A820 (32) OneMapDeep version of GetNamedResource
    .globl Get1Resource // $A81F (31) OneMapDeep version of GetResource
    .globl GetIndResource // Given index and type, returns handle
    .globl GetIndType // Given index, returns type
    .globl GetNamedResource // Given name and type, return handle
    .globl GetResAttrs // Given handle, get the attributes of a resource
    .globl GetResFileAttrs // Given refnum, return resfile attributes.
    .globl GetResInfo // Given handle, get the info of a resource
    .globl GetResource // Given an ID and type, give handle back
    .globl HomeResFile // Returns refnum of an object's home resfile
    .globl InitResources // One-time call, initializes resource manager
// and opens Sys.resource (!refnum)
    .globl InitRSRCMgr // initializes the Resource Manager, called from StartInit
    .globl LoadResource // Given handle, loads resource from source
    .globl MaxSizeRsrc // $A821 (33) size of rsrc without hitting disk
    .globl OpenResFile // Given filename, opens and returns refnum
    .globl OpenRFPerm // Given filename, permissions, and volrefnum,
// ... do OpenResfile
    .globl ReleaseResource // Given handle, releases resource.
    .globl ResError // Returns error code, 0 if no error.
    .globl rGetResource // $A80C (12) getresource/reverse sys & rom map.	<14jan87> BBM
    .globl RmveResource // Remove a resource from the resFile
    .globl RmveReference // a nop routine added back in for MacWrite 2.2	<17oct85> BBM
    .globl RsrcMapEntry // given handle, return offset to rsrc entry
    .globl RsrcZoneInit // Zero all handles in the system map
// ... which point to application heap zone
    .globl SetResAttrs // Given handle, set the attributes of a resource
    .globl SetResFileAttrs // Given refnum, set resfile attributes.
    .globl SetResInfo // Given handle, set the info
    .globl SetResLoad // Auto-Load on get?
    .globl SetResPurge // Write resources when purging?
    .globl SizeResource // Returns 4 byte resource size (may go to disk!!)
    .globl Unique1ID // $A81E (30) OneMapDeep version of UniqueID
    .globl UniqueID // Given type, return a unique ID for that type
    .globl UpdateResFile // Given refnum, update resource file
    .globl UseResFile // Given refnum, sets current res file.
    .globl WriteResource // Given handle, writes resource to file

    .globl vSuperLoad // vector to routine superload					<27oct85> BBM
    .globl vCmpFrm // vector to inside UpdateResFile				<27oct85> BBM
    .globl vNewMap // vector to routine NewMap						<27oct85> BBM
    .globl vCheckLoad // vector to the checkload routine.				<27oct85> BBM

    .globl vRMgrStdEntry // vector to the StdEntry routine.				<1.2> BBM
    .globl vRMgrStdExit // vector to the StdExit routine.				<1.2> BBM
    .globl StdZEntry // <9> rb
    .globl Std1Entry // <9> rb
    .globl RStdExit // <9> rb
    .globl RdData // <9> rb
    .globl WrData // <9> rb
    .globl SaveRegs // <9> rb
    .globl SpaceAt // <9> rb
    .globl CheckGrowAt1 // <9> rb <SM26> rb
    .globl RefHandle // <9> rb
    .globl AddNewRef // <9> rb
    .globl AddNewRefWithoutUpdate // <SM48> kc
    .globl AddName // <9> rb
    .globl RRentry6 // <9> rb
    .globl ROMOpenRFPerm // <9> rb
    .globl HCreateResFile //<SM9> <PN>

    .globl FPCallROvr // <SM27> bt
    .globl FPCallCacheFlush // <SM27> bt
    .globl FPCallDonnDecompress // <SM27> bt
    .globl FPCallGreggyPrepare // <SM27> bt
    .globl FPCallGreggyDecomp // <SM27> bt
    .globl FPCallGreggyDone // <SM27> bt
    .globl FPCallResErrProc // <SM27> bt
    .globl FPCallCountIExit // <SM27> bt
    .globl FPvCmpFrm // <SM38> bt
    .globl SuperLoad // <SM38> bt
    .globl NewMap // <SM38> bt
    .globl CheckLoad // <SM38> bt


//_______________________________________________________________________________
// Register conventions throughout the Resource Manager:
//
// A0, D0, D1, D2, D7 scratch.
//
// A1 is the handle to the resource
// A2 points to an entry in the map
// A3 points to the beginning of a sub-block
// A4 is the map handle
//
// D3 is the resource type, if any
// D4 is the number of resource entries (-1) for looping
// D5 is the number of types (-1) for looping
// D6 is the resource map reference number
//
// A6 is always LINKed, through StdEntry.  There is only 1 local variable,
// an IOStackFrame.	A0 also always points to 8(A6) when returning from StdEntry.
//_______________________________________________________________________________


//_______________________________________________________________________________
//
// NullRF		the null resource file.
// NullStr		just points to the word 0.
// RMGRVersion	the resource manager version number
//_______________________________________________________________________________


// BLANKS ON
// STRING ASIS
// MACHINE MC68020

    .equ NullRMSize, 30 // number of bytes in a null map
NullStr:
 // Null String (pascal string 1st byte 0)
NullRF:
 // Null Resource map
    .word 0, DataFirstRes // ResDataOffset
    .word 0, DataFirstRes // ResMapOffset (no data!)
    .word 0, 0 // DataSize
    .word 0, 32 // MapSize (was NullRMSize=30, now 32 for full template)
    .word 0, 0 // MNext						<1.1>
    .word 0 // MRefNum
    .word 0 // MAttr
    .word 28 // MTypes: type list at offset 28 (past this header)
    .word 30 // MNames: name list at offset 30
    .word -1 // type count-1 = $FFFF (no types)
    .word 0 // null name string

    .word 1 // Resource Manager version number

//_______________________________________________________________________________
//
// Routine:		InitRSRCMgr
//
// Arguments:	none
//
// Called By:	system initialization sequence in StartInit
// Calls:		InitResources
// Destroys:		D0-D2/A0-A1
//
// Function:		Call the Resource Manager to load ROM rsrcs and set up his
//				indefatigable world.
//_______________________________________________________________________________

InitRSRCMgr:
    move.l %sp,0x1F10.w          /* entry SP */
    /* Check ScrnBase on entry */
    movem.l %a0/%d0,-(%sp)
    pea .Ldbg_scrn(%pc)
    move.l (%sp)+,%a0
    move.l 0x0824.w,%d0          /* D0 = ScrnBase */
    .word 0xA0FE
    movem.l (%sp)+,%a0/%d0
    move.l #0x00FFFFFF,Lo3Bytes  /* init Lo3Bytes — SE ROM doesn't set this */
    move.l #0x00010001,OneOne   /* init OneOne — SE ROM doesn't set this */
    /* ExpandMem: use zeroed area in system zone instead of allocating
     * (the _NewPtrSysClear trap corrupts SP — needs investigation) */
    move.l ExpandMem,%d0
    beq.w .Lneed_em
    cmpi.l #0xFFFFFFFF,%d0
    bne.w .Lhave_em
.Lneed_em:
    move.l #0x2200,ExpandMem     /* use system zone area (zeroed) */
.Lhave_em:
    move.l %sp,0x1F14.w          /* SP after ExpandMem */
    /* Install RM traps into Toolbox trap table NOW (not at boot).
     * The old ROM RM handled any earlier Toolbox trap calls. */
    movem.l %d0/%a0-%a1,-(%sp)
    lea rm_trap_table(%pc),%a0
1:  move.w (%a0)+,%d0           /* table offset from $0E00, $FFFF = end */
    bmi.w 2f
    lea 0x0E00.w,%a1
    adda.w %d0,%a1
    move.l (%a0)+,(%a1)         /* write handler address to Toolbox table */
    bra.w 1b
2:  movem.l (%sp)+,%d0/%a0-%a1

    /* Install _ResourceDispatch ($A822) stub — the SE ROM's handler
     * doesn't understand System 7 override selectors and crashes.
     * Point it to our RTS stub. */
    lea ResourceDispatchStub(%pc),%a0
    move.l %a0,(0x0E00 + 0x022 * 4).w

    /* Set up RM dispatch vectors */
    lea vRMgrStdEntry(%pc),%a0
    move.l %a0,JRMgrStdEntry
    lea vRMgrStdExit(%pc),%a0
    move.l %a0,JRMgrStdExit
    lea vNewMap(%pc),%a0
    move.l %a0,jNewMap
    lea vCheckLoad(%pc),%a0
    move.l %a0,jCheckLoad
    lea vSuperLoad(%pc),%a0
    move.l %a0,jSuperLoad
    lea vCmpFrm(%pc),%a0
    move.l %a0,jCmpFrm

    move.l %sp,0x1F18.w          /* SP after vectors */

    st SysMap // flag that resources are not inited
    clr.l TopMapHndl // tell the res mgr that it's start time

    move.b MMFlags,-(%sp) // save the memory Manager flag
    bset.b #MMStartMode,MMFlags // set for 32 bit system heap

    move.l ExpandMem,%a0 // init override flags 		<SM48> kc
    tst.l %a0                // SE ROM has no ExpandMem
    beq.w 0f
    clr.b emScanOverrideMaps(%a0) // turn off ScanOverrideMaps	<SM48> kc
    clr.w emLastMapOverridden(%a0) // flush override cache.		<SM48> kc
    clr.w emOverrideMapRefNum(%a0) // flush override cache.		<SM48> kc
0:
//							<SM48> kc
    movem.l %a0/%d0,-(%sp)
    pea .Ldbg_pre_ir(%pc)
    move.l (%sp)+,%a0
    move.l %sp,%d0
    .word 0xA0FE                 /* trace: SP before _InitResources */
    movem.l (%sp)+,%a0/%d0

    clr.w -(%sp) // Save room for function result
    _initresources // Init ROM resource file

    movem.l %a0/%d0,-(%sp)
    pea .Ldbg_post_ir(%pc)
    move.l (%sp)+,%a0
    move.l %sp,%d0
    .word 0xA0FE                 /* trace: SP after _InitResources */
    movem.l (%sp)+,%a0/%d0

    addq #2, %sp // Assume success
    move.b (%sp)+,MMFlags // restore memory manager flag
    /* The ROM's screen init at +$05CA gets bypassed because an RM
       trap handler between $00F6 and $0106 doesn't return correctly.
       Do the essential screen setup here before returning. */
    move.l 0x010C.w,0x0824.w   /* BufPtr → ScrnBase */
    move.w #0x0040,0x0106.w    /* ScreenRow = 64 bytes */
    rts


//_______________________________________________________________________________
//
// Routine:		FUNCTION InitResources: INTEGER;
//
// Arguments:	8(A6)	(output.W)		Refnum of Sys.rsrc map
//
// Called By:	A-trap through Dispatcher
// Calls:		StdEntry, NewMap, PreLoad, StdZeroExit, BuildMap
//
// Function: 	Now a Two-time call, in order to support ROM Resources.  First,
//				it sets up the ROM resources as the Current and System (& only)
//				Map in memory.	Second call initializes resource manager & opens
//				Sys.rsrc switching pointers to make it the System map and the
//				Rom map and the current map.
//				Returns the reference number of the Sys.rsrc map.
//				Sets ResLoad to TRUE initially, in PreLoad.
//_______________________________________________________________________________

    .globl SmgrInitialize //

InitResources:
    /* Debug: write marker to $0F00 to verify this code runs */
    move.l #0xCAFE0995,0x1F00.w

// deleted the stuff here that called FixSMgrWorld: was never executed	<07/02/89 pke>

    clr.l HSCHandle // Invalidate cache								<12mar85> BBM
    move.l #0xCAFE0001,0x1F04.w  /* marker: before StdZEntry */
    bsr StdZEntry // Save regs 									<11jul85> BBM
    move.l #0xCAFE0002,0x1F08.w  /* marker: after StdZEntry */
    tst.l TopMapHndl // are the ROM resources "loaded in"?			<31jan85> BBM
    bgt.w .L_InitResources_skipROM // CC=LE means to build rom map from scratch 	<27aug85> BBM

    clr.l ResErrProc // clear for start code							<27aug85> BBM
    move.l #0xCAFE0003,0x1F0C.w  /* marker: before ROZInit */
    bsr ROZInit // initialize a Read-Only-Zone
    move.l %a0,TopMapHndl // Save handle as top map
    move.l %a0,SysMapHndl // and as system map
    move.l OneOne,SysMap // Make SysMap & Curmap refnum = 1
    pea .Ldbg_redomap(%pc)
    move.l (%sp)+,%a0
    moveq #4,%d0
    .word 0xA0FE                     /* trace: before ReDoMap */
    bsr ReDoMap // set up map from rom image (Returns A1 Ptr)
    pea .Ldbg_aftermap(%pc)
    move.l (%sp)+,%a0
    moveq #5,%d0
    .word 0xA0FE                     /* trace: after ReDoMap */
    /* Now mark the ROZ as read-only (deferred from ROZInit) */
    move.l 0x1F44.w,%a0
    bset #ROZ,flags(%a0)
    clr.l MNext(%a1) // Zero next map handle
    pea .Ldbg_exit(%pc)
    move.l (%sp)+,%a0
    moveq #6,%d0
    .word 0xA0FE                     /* trace: before InitResources exit */
    bra.w .L_InitResources_exit // and exit through StdExit

.L_InitResources_skipROM:

    lea theZone, %a3 // ... else do full load,Point to theZone.
    move.l (%a3), -(%sp) // Save the current zone
    move.l SysZone, (%a3) // Set system heap for system resource map.
    lea SysResName, %a1 // Point to name of system resource.
    moveq #0x00,%d0 // nil dir id 	<SM9> <PN>
    bsr NewMap // Load in map, refnum in D6, result in D0.
    move.l (%sp)+, (%a3) // Restore the zone
    move.w %d6, 8(%a6) // Return result/error code.
    tst.l %d0 // Test newMap result in D0.
    ble.w .L_InitResources_exit // if <=0, error or already open.

    move.l %a4,TopMapHndl // Save handle as top map						<05sep85> BBM
    move.l %a4,SysMapHndl // Save handle as sys map						<05sep85> BBM
    move.w %d6,CurMap // set up curmap 								<09jul85> BBM
    move.w %d6,SysMap // set up sysmap 								<10jul85> BBM
    bsr PreLoad // Preload required resources... 				<31jan85> BBM


    bsr ReDoMap // restore the ROM map (Returns A1 pointer)
    move.l SysMapHndl,MNext(%a1) // next map in link from rom is system
    move.l SysMapHndl,%a1 // get handle to system map
    move.l (%a1),%a1 // and dereference system handle
    clr.l MNext(%a1) // sysmap is last link in chain

    bsr.w RomOverRide // check which rom rsrc's to override

    move.l IntlSpec,%d0 // if IntlSpec is not set up,	<1.2><1.3>
    addq #1,%d0 //	go set it up.				<1.2><1.3>
    jsr SmgrInitialize // <SM22>
.L_InitResources_exit:


//	Rolled in InstallProc  rolled in to set emScanOverrideMaps to true here.

    move.l ExpandMem,%d0
    beq.w .L_InitResources_noExpandMem
    move.l %d0,%a0
    /* DON'T enable override scanning — SE has no _GetOverrideMap trap.
       Original: move.w #-1,emScanOverrideMaps(%a0)  */
    clr.b emScanOverrideMaps(%a0)

.L_InitResources_noExpandMem:

    bra StdZeroExit // ...and exit.

//_______________________________________________________________________________
//
// Routine:		RomOverRide (utility)
//
// Arguments:	None
//
// Called By:	InitResources
// Calls:		_GetResource
//
// Function: 	Checks to see if there is a override resource in the new file
//				opened.  If there is, it executes that resource.
//
// NOTE: 		The override resource must be preloaded, locked, & nonpurgable.
//_______________________________________________________________________________

    .equ ROVR, 0x524F7672 // ROVR = 'ROvr' (type of override rsrc)

RomOverRide:
 // new routine									<27aug85> BBM
    subq #4,%sp // Save room on stack for handle
    move.l #ROVR,-(%sp) // rsrc type for override
    move.w #0,-(%sp) // rsrc ID of 0
    _getresource // get the override rsrc
    move.l (%sp)+,%d0 // Check to see if it exists
    beq.w ROvrOut // if handle is zero then just return
    move.l %d0,%a0 // get ready to execute this resource
    move.l (%a0),%a0 // dereference handle
FPCallROvr:
 // FlashPort hint attachment point <bt>
    jsr (%a0) // and execute handle
ROvrOut:
    rts // and return

//	<1.5> Equates

    /* ComBFSz, MemHeadSz, MaxComInd defined in resmgr_equ.inc */
    .equ RNmOff, 0xF // offset from past the combo field to the name size	<3>

//_______________________________________________________________________________
//
// Routine:		ROZInit (utility)
//
// Arguments:	A0		(output.L)		Handle to the Map in the ROZ
//				A2		(output.L)		Pointer to the Map up in the ROM
//				RomMapHndl (output.L)	Handle to the Map in the ROZ
//
// unchanged:	A3-A7,D1-D7
// trashes:		A1-A2,D0
//
// Called By:	InitResources
// Calls:		_BlockMove, _InitZone, _NewHandle, _NewPtr
//
// Function: 	Builds a new zone in the system heap.  This zone is a Read Only
//				Zone.  The size of the new zone can be calculated by the fomula:
//
//<A291/29oct86> bbm memory manager is now long word aligned, which changes ROZinit.
//<C396/4dec86>	agh Make master pointer size 8 bytes for use with UNIX 32 bit
//					memory mgr.
//
//_______________________________________________________________________________
//
//		Size = zoneheader + zonefooter + longwordalignbias + slop
//				+ (12 + block of 4 master pointers)
//				+ (12 + size of master pointer block) + (12 + size of map)
//		Size = 52 + 12 + 12 + (12 + 32) + (12 + sizempb) + (12 + sizemap) + slop			<c396>
//		Size = 144 + sizebmp + sizemap	+ slop												<c396>
//		Size = $90 + 8*X + Y+slop		; (where X = number of rsrc's 						<c396>
//		Size = 8*(#$18 + X) + Y 		;	 and Y = size the map)    						<c396>
//
//The old stucture of the ROM is as follows:
//		ROMRsrcStart:	(long)			; offset to Rom Rsrc stuff
//		Rom Rsrc Stuff
//			X:			(2)		number of rsrc's in rom
//			Y:			(2)		size of rom rsrc map in rom
//			Map:		(Y)		the rom map
//			rsrc's:		(x)		the rsrc's
//
//The new stucture of the ROM is as follows:
//		RomBase
//			1A			(4)		offset to Rom Resource Structure Table
//
//		Rom Resource Structure Table
//			0			(4)		offset to first Resource Entry
//			4			(1)		maximum valid index in parameter ram [v]
//			5			(1)		Size of Combo Bit Field (v)
//			6			(2)		Version
//			8			(2)		Size of memory block header
//
//		Hiram Resource Entry (all are offsets from base of rom)
//			0			(v)		combo bits
//			v			(4)		link to next Resource
//			v+4			(4)		offset to data for this rsrc
//			v+8			(4)		Resource Type
//			v+C			(2)		Resource ID
//			v+E			(1)		Resource Attributes
//			v+F			(1)		length of Resource name [n]
//			v+$10		(n)		Resource name
//			v+$10+n		(0-3)	align 4
//			g(v+$10+n)	(8/12)	memory block header
//			g(v+$10+n)+8 (y)	Resource Data [or g(v+C+n) + 12, depending on which mem mgr]
//			x+y			(0-3)	align 4
//
//				where g(w) = w + ((4 - (w mod 4)) mod 4)
//
//_______________________________________________________________________________

ROZInit:
 // new routine									<27aug85> BBM  <1.5>

    bsr CountCombos // to determine the sizes  <1.5>
    move.l %d0,%d5 // save count in D5
    move.l %d1,%d6 // save map size in d6

    moveq #0x18,%d0 // clear high word (D0 will be size of ROZ)   	<1.5>
    add.w %d5,%d0 // add in number of MP's
    asl.l #3,%d0 // multiply by 8 (the size of a MP)
    add.w %d6,%d0 // Add in size of map [D0 = 8*(#$10 + X) + Y]

// Note to accomodate Figment, we make the ROZ larger. -JC
    add.l #1000,%d0

    move.l %d0,%a1 // Save off Zone size for later
    _NewPtrSys // no error checking.  If failure, system dead.

    move.l theZone,-(%sp) // save current zone till after map build
    clr.l -(%sp) // pGrowZone = 0
    move.w #4,-(%sp) // cMoreMasters = 4
    adda.l %a0,%a1 // limitPtr = startPtr + Size
    move.l %a1,-(%sp) // ... and storing to parameter block
    move.l %a0,-(%sp) // setting up parameter block for _InitZone
    move.l %a0,%a1 // save pointer to zone for later
    move.l %sp,%a0 // Point A0 to the paramter
    _initzone // finally get to set up zone
    /* DON'T set ROZ flag yet — Figment blocks SetHandleSize in read-only zones.
       ReDoMap needs to grow the map handle while adding resources.
       We set the ROZ flag after ReDoMap completes (in InitResources). */
    move.l %a1,0x1F44.w // save zone pointer for deferred ROZ flag
    move.l %a1,theZone // we want ROM Rsrcs in this new zone
    add.w #0xE,%sp // pop off parameter block from stack

    move.l %d5,%d0 // get number of MP's							<1.5>
    asl.l #3,%d0 // Get the size (size = 8*X)						<c396>
    _newptr // get MPB in ROZ. (If failure, system dead.)
    move.l %a0,0x1F40.w // save MP block base for ReDoMap

    move.w %d6,%d0 // get the size of the map						<1.5>
    _newhandle // get map in ROZ.  (If failure, system dead.)
    _hlock // it‚Äôs always there, so lock it					<C169>
    move.l %a0,RomMapHndl // Save this handle.
    move.l (%sp)+,theZone // restore current zone
    rts


//_______________________________________________________________________________
//
// Routine:		ReDoMap (utility)
//
// Arguments:	A1		(output.L)		pointer to the map in the ROZ
//
// unchanged:	A5-A7,D6-D7
// trashes:		A0,A2-A4,D0-D5
//
// Called By:	InitResources
// Calls:		_SetHandleSize, _BlockMove, GetRsrcCnt, SetPL
//
// Function: 	This routine builds Rom map in the ROZ.  ReDoMap assumes that
//				the ROZ is already built, and ignores all previous data in ROZ.
//_______________________________________________________________________________

ReDoMap:
 // new routine
    bsr CountCombos // to determine the sizes
    move.l %d1,%d0 // get approximate size of map into d0
    move.l RomMapHndl,%a0 // Get the handle to the ROM Map in ROZ
    move.l %a0,%a4 // Save for later
    _sethandlesize // Old resource map could have overrides

    moveq #0x20,%d0 // size of null map
    lea NullRF,%a0 // get null map from rom
    move.l (%a4),%a1 // Dereference handle to destination
    _blockmove // move map down
    move.w #1,MRefNum(%a1) // and stuff the rom map refnum (so getcurmap works)

    clr.w -(%sp) // get a buffer on the stack to read in index
    move.l %sp,%a0 // point to stack
    move.l #0x000100AE,%d0 // get selected resource combo index
    _readxpram //
    move.b (%a0),%d3 // save index in d1
    addq #2,%sp // dispose of buffer
    moveq #1,%d3 // SE: force combo index 1 (no UnivInfoPtr)

// add all the resources, set up the regs to look like:
//	a2.l	pointer to resource header
//	d2.l	combo field size
//	d3.b	index
//	d4.w	size of memory block header (in bytes)

    move.l RomBase,%a2 // get pointer to romstart
    adda.l #0x50000,%a2 // combo format at fixed ROM offset (skip ROM+$1A)
    moveq #0,%d2 // zero combo bitfield size
    move.b ComBFSz(%a2),%d2 // get combo bitfield size
    move.w MemHeadSz(%a2),%d4 // get size of memory header
    tst.b %d3 // combo zero means use default (PRAM init'ed)	<1.6>
    beq.w .L_ReDoMap_default // if invalid PRAM, use machine specific default	<1.6>
    cmp.b MaxComInd(%a2),%d3 // check if d3 too big
    bls.w .L_ReDoMap_good //												<SM42>
.L_ReDoMap_default:
 // if out of range, or invalid, use default		<1.6>
    movea.l UnivInfoPtr,%a0 // point to the universal ProductInfo record		<1.6>
    move.b DefaultRSRCs(%a0),%d3 // get the default combo					<1.6>
.L_ReDoMap_good:
 //
// Test for an optional FPU.  Combos 3 and 4 are identical except 3 inludes FPU			<2>
// Combos are defined in Rom.r.  **** If combos change, make sure test is valid !!!***	<2>
    cmpi.b #4,%d3 // combo with optional FPU?						<2>
    bne.w nochange // branch if not combo with optional FPU			<2>
    btst.b #hwCbFPU-8,HWCfgFlags // FPU installed on board?						<2>
    beq.w nochange // branch if no FPU installed					<2>
    moveq #3,%d3 // change to combo with FPU 						<2>
nochange:
 //												<2>
    move.l (%a2),%a2 // offset to the first resource.
    add.l RomBase,%a2 // make offset a pointer
.L_nochange_loop:

//first check if this combo is valid

    move.l %a2,%a0 // get combo field pointer
    moveq #0,%d0 // clear upper bytes
    move.b %d3,%d0 // get index
    lsr.b #3,%d0 // div by 8
    add.w %d0,%a0 // bump a0 to the right byte
    move.b %d3,%d0 // get index into this byte (index is 0-7)
    eori.b #0xFF,%d0 //   reverse index
    andi.b #7,%d0 //     make index correct (index is now 7-0)
    btst.b %d0,(%a0) // check if this resource should be included

    add %d2,%a2 // point at link to next entry (no cc‚Äôs set)
    beq.w .L_nochange_bypass // this resource not in current combo, so skip

//add this resource

    move.l 4(%a2),%a3 // get ptr to resource data
    adda.l RomBase,%a3 // make offset a pointer (cc's not changed)
// set up master pointer — use saved MP block base instead of RelHandle
    move.l 0x1F40.w,%a0 // MP block base (saved by ROZInit)
    move.l %a3,(%a0) // stuff MP with pointer to rsrc in ROM
    addq.l #8,0x1F40.w // advance to next MP slot

    move.l %a0,-(%sp) // push the handle
    move.l 0x8(%a2),-(%sp) // push the Resource Type
    move.w 0xC(%a2),-(%sp) // push the Resource ID
    pea 0xF(%a2) // push pointer to Resource name

/* IF 0 THEN — MPW conditional, must NOT execute in GAS
    move.l RomMapHndl,%a0
    move.l (%a0),%a0
    moveq #0,%d7
    move.w 0x1c(%a0),%d7
   ENDIF */

    movem.l %a0/%d0,-(%sp)
    pea .Ldbg_addrom(%pc)
    move.l (%sp)+,%a0
    move.l 0x8(%a2),%d0          /* D0 = resource type */
    .word 0xA0FE
    movem.l (%sp)+,%a0/%d0
    bsr.w AddRomRsrc // add the resource

/* IF 0 THEN — MPW conditional, must NOT execute in GAS
    move.l RomMapHndl,%a0
    move.l (%a0),%a0
    sub.w 0x1c(%a0),%d7
    lsl.w #3,%d7
    add.w #12,%d7
    add.l %d7,MapSize(%a0)
    move.l -8(%a3),%d0
    _stripaddress
    addq #4,%d0
    add.l %d0,ResMapOffset(%a0)
    add.l %d0,DataSize(%a0)
   ENDIF */

.L_nochange_bypass:
 //
    move.l (%a2),%a2 // offset to next resource header
    move.l %a2,%d0 // check if no more in the list
    adda.l RomBase,%a2 // make offset a pointer (cc's not changed)
    bne.w .L_nochange_loop // go find next link

    pea .Ldbg_loopdone(%pc)
    move.l (%sp)+,%a0
    move.l %d5,%d0               /* D0 = resource count */
    .word 0xA0FE                 /* trace: ReDoMap loop done, count in D0 */
    move.w #1,MRefNum(%a1) // save the refnum of the rom file in map
    rts // and return

//_______________________________________________________________________________
//
// Routine:		CountCombos (utility)
//
// Arguments:	D0		(output.W)		number of resources in the rom.
//				D1		(output.L)		Approximate Size of Rom Resource Map
//
// unchanged:	A0-A7,D2-D7
// Trashed:		d0-d1	(upper word trashed, lower word is output)
//
// Called By:	ReDoMap, ROZInit
// Calls:		not a soul.
//
// Function: 	Go Through the linked list and count the number in the list and
//				get the total size of all the names.
//_______________________________________________________________________________

CountCombos:
 // new routine									<1.5>
    movem.l %d2-%d4/%a2,-(%sp) // save d0-d1/a0 across call
    move.l RomBase,%a2 // get pointer to romstart
    adda.l #0x50000,%a2 // combo format at fixed ROM offset (skip ROM+$1A)
    moveq #0,%d2 // zero combo bitfield size
    move.b ComBFSz(%a2),%d2 // save combo bitfield size
    move.l (%a2),%a2 // offset to the first resource.
    add.l RomBase,%a2 // make offset a pointer
    moveq #0,%d0 // clear count
    moveq #0,%d1 // clear total name size
    moveq #0,%d3 // clear temp name size

.L_CountCombos_loop:

    addq #1,%d0 // add one to the count of the resources
    add %d2,%a2 // jump past the combo bit field
    move.b RNmOff(%a2),%d3 // get the name size (only get one byte!)
    bmi.w .L_CountCombos_resourceNotNamed // don‚Äôt add the name size if not named			<13> stb
    add.l %d3,%d1 // add into total name size					<3>	<13> stb
.L_CountCombos_resourceNotNamed:
 //												<13> stb
    move.l (%a2),%a2 // offset to next resource header
    move.l %a2,%d4 // check if no more in the list
    add.l RomBase,%a2 // make offset a pointer
    bne.w .L_CountCombos_loop // go find next link

// to get approx size of map
//	use headersize + 8*n + 12*n + namesize
//			28 + (20)*n + d1
//	where n is number of resources and d1 has the size of names.
    move.l %d0,%d2 // get number of resources
    mulu #0x14,%d2 // multiply by size of type entry and resource entry
    addi.l #0x1c,%d2 // add in map header
    add.l %d2,%d1 // add into the size of names

    movem.l (%sp)+,%d2-%d4/%a2 // restore registers
    rts // and return

//_______________________________________________________________________________
//
// Routine:		PROCEDURE AddRomRsrc(theResource:Handle; theType:LONGINT;
//										theID:INTEGER; name:Str255);
//
// Arguments:	18(A6)	(input.L)		handle to the resource
//				14(A6)	(input.W)		theType
//				12(A6)	(input.W)		theID
//				8(A6)	(input.L)		pointer to pascal string
//
// Called By:	ReDoMap
// Calls:		StdEntry, GetCurMap, AddNewRef, AddName, StdExit
//
// Function: 	Add a reference to the given resource in the rom map.
//_______________________________________________________________________________


    .equ RomAttrs, 0x58 // the default rom resource attributes
// (protected, locked, sysheap)

AddRomRsrc:

    move.w #MapTrue,RomMapInsert // don't insert ROM Map
    bsr StdEntry // Link A6, save regs
    move.l 18(%a6),%a1 // Get handle in A1 (has to be valid!)

    movem.l %a0/%d0,-(%sp)
    pea .Ldbg_ar1(%pc)
    move.l (%sp)+,%a0
    move.l %a1,%d0              /* D0 = handle */
    .word 0xA0FE                /* trace: AddRomRsrc handle */
    movem.l (%sp)+,%a0/%d0

    bsr GetCurMap // Get the current map for diskFull check

    movem.l %a0/%d0,-(%sp)
    pea .Ldbg_ar2(%pc)
    move.l (%sp)+,%a0
    move.w ResErr,%d0           /* D0 = ResErr after GetCurMap */
    .word 0xA0FE
    movem.l (%sp)+,%a0/%d0

    move.l 14(%a6),%d3 // Put type of resource to add in D3

    movem.l %a0/%d0,-(%sp)
    pea .Ldbg_ar3(%pc)
    move.l (%sp)+,%a0
    move.l %d3,%d0              /* D0 = type */
    .word 0xA0FE                /* trace: before AddNewRef */
    movem.l (%sp)+,%a0/%d0

    bsr AddNewRef // Set up a new reference to the resource
// Returns A2-->reference location.

    movem.l %a0/%d0,-(%sp)
    pea .Ldbg_ar4(%pc)
    move.l (%sp)+,%a0
    move.l %a2,%d0              /* D0 = ref location */
    .word 0xA0FE                /* trace: after AddNewRef */
    movem.l (%sp)+,%a0/%d0

    move.w 12(%a6),RID(%a2) // Set ID of reference
    move.w #-1,RNameOff(%a2) // Assume no name yet

    move.l 8(%a6), %a0 // Get pointer to reference name
    bsr AddName // Name set in RNameOff(A2).
// A2, A3 preserved and are correct.
    move.l 18(%a6),%a0 // get the handle to the resource
    move.l %a0,RHndl(%a2) // set handle.
    move.l (%a0),%a0 // point at the resource
    sub.l rombase,%a0 // make a0 an offset into rom
    move.l %a0,RLocn(%a2) // store offset into map
    move.b #RomAttrs,RAttr(%a2) // set rom resource attributes
.L_AddRomRsrc_9:
    moveq #14, %d0 // Pop off 14 bytes of parameter
    bra StdExit // Restore regs, unlink A6


//_______________________________________________________________________________
//
// Routine:		PreLoad (Utility)
//
// Arguments:	There are no input arguments.
//				The world is left in a weird state on exit (no regs dependable).
//
// Called by:	(Preload): InitResources
//				(Preload2): OpenResFile
// Calls:		GetMap, FirstRes, CheckLoad, NextRes
//
// Function: 	Clear the handle locations for all local (i.e. non-resSysRef)
//				resource entries, and load in all resources in the current map
//				which have the resPreload bit set.
//
//				PreLoad walks through the map in file order.   This was done to
//				optimize the file I/O and buffer hits.
//_______________________________________________________________________________

Preload:

    st ResLoad // Set ResLoad to True (byte operation)
Preload2:
 // alternate entry for OpenResFile
    bsr GetMap // Get the current map							<06may85> BBM
    bmi.w .L_Preload2_9 // if can't find map then exit
    bsr GetRsrcCnt // get the first resource entry					<10may85> BBM
    bmi.w .L_Preload2_9 // if no entries just exit
.L_Preload2_1:
    addq #RHndl,%a2 // point at the resource entry handle

    clr.l (%a2)+ // clear it and point and next rsrc entry
    dbra %d4,.L_Preload2_1 // if more, loop back, else fall through

    tst.b ResLoad // if (ResLoad = False) just return				<22apr85> BBM
    beq.w .L_Preload2_9 // ... sure enough resload is false (CC=EQ)
    bsr.w SuperLoad // if we can do fast load then this does it		<24jun85> BBM
    beq.w .L_Preload2_9 // CC=EQ means that we have loaded in rsrcs

.L_Preload2_2:
    moveq #ResPreLoad,%d1 // Which bit to test on the lowest level 		<09apr85> BBM
    bsr FirstRes // Point A2 to 1st preload  file-order entry 	<08mar85> BBM
    bmi.w .L_Preload2_9 // if none, exit.
.L_Preload2_3:
    bsr CheckLoad // load in the resource.
    moveq #ResPreLoad,%d1 // Which bit to test on the lowest level 		<29apr85> BBM
    bsr NextRes // Skip to next preload entry in file order. 	<08mar85> BBM
    beq.w .L_Preload2_3 // If there are more, loop back
.L_Preload2_9:
    rts // ...and return.

//_______________________________________________________________________________
//
// Routine:		SuperLoad (internal)													<06may85> BBM
//
// Arguments:	A4		(i/o.L) 		Handle to the new map
//
// Unchanged:	A4-A7,D6
// Trashed:		A0-A3,D0-D5,D7
//
// Called By:	Preload/Preload2
// Calls:		_BlockMove, _DisposHandle, GetEntries, GetMaxLoad, _NewEmptyHandle,
//				_NewHandle, _NewPtr, NextEntry, NotFound, SetPL, SetupInBuff
//
// Function: 	Try to load all of the resources you can in a more intelligent
//				fashion.  If you cann't then exit with CC=MI.  This means you'll
//				have to do the old stupid preload.	GetMaxLoad determines if
//				this is a new format, how much of the file to read in, and reads
//				the data into a buffer.
//
// future note - Try taking advantage of all the information in the cache.
//_______________________________________________________________________________

SuperLoad:

    move.l jSuperLoad,%a0 // get the vector from the OSJumptable			<27oct85> BBM
    jmp (%a0) // and go to the routine 						<27oct85> BBM
vSuperLoad:
 // Def to build dispatch entry					<27oct85> BBM
    bsr GetMaxLoad // load in as much as we can for super load		<24jun85> BBM
    bmi NotFound // if nothing to SuperLoad return (old format)

    bsr GetEntries // get the first resource entry					<07may85> BBM
    move.l (%a4),%a1 // dereference map handle
.L_vSuperLoad_1:
    move.l RLocn(%a2),%d0 // check if this rsrc is in the buffer			<24jun85> BBM
    and.l Lo3Bytes,%d0 // mask off attributes
    cmp.l HSCache,%d0 // offset to end of buffer
    bge .L_vSuperLoad_7 // skip to the next one 							<SM25> CSS
    move.l theZone,-(%sp) // save theZone for later						<08may85> BBM
    btst #ResSysHeap,RAttr(%a2) // which zone do we put this rsrc?
    beq.w .L_vSuperLoad_2 // skip if default zone.
    move.l SysZone,theZone // else force system heap
.L_vSuperLoad_2:
    move.l RLocn(%a2),%d2 // Get offset into file
    and.l Lo3Bytes,%d2 // clear high bits.
    bsr SetupInBuff // setup D0=size; A0=Ptr2Rsrc					<SM25> CSS
    btst #ResLocked,RAttr(%a2) // if locked do NewPtr ...
    suba.l (%a4),%a2 // (Make A2 an Offset)
    suba.l (%a4),%a3 // (Make A3 an Offset)
    beq.w .L_vSuperLoad_3 // ... else do NewHandle 						<15may85> BBM

// We have a locked resource.  Do contortions to	<c396>
// get the resource loaded low (NewPtr style).	<c396>
    move.l %d0, %d1 // Save Resource Size.							<c396>
    _newemptyhandle // NewEmptyHandle / DisposeHandle pair insures	<c396>
    bmi .L_vSuperLoad_9 // that a master pointer will be available		<c396> <SM25> CSS
    _disposhandle //   without allocating a new master pointer blk	<c396>
    bmi .L_vSuperLoad_9 //   when we do the for-real _NewHandle.			<c396> <SM25> CSS
    move.l %d1, %d0 // Grab block size.								<c396>
    _newptr // NewPtr allocates block of desired size low	<c396>
    bmi .L_vSuperLoad_9 //   and leave roving allocate ptr set to it.	<c396> <SM25> CSS
    _disposptr // DisposPtr leaves the perfect free block 		<c396>
    bmi.w .L_vSuperLoad_9 //   for subsequent NewHandle to allocate.		<c396>
    move.l %d1, %d0 // Restore block size into D0					<c396>

.L_vSuperLoad_3:
    _newhandle // Get a handle for this rsrc (D0 already setup)
    bmi.w .L_vSuperLoad_9 // any error go do regular preload stuff
    move.l %a0,%d1 // save off Handle to stuff into map
    move.l (%a0),%a1 // Get Destination into A1 for BlockMove

.L_vSuperLoad_6:
    bsr.w SetupInBuff // note destination already has A1
    _blockmove
    adda.l (%a4),%a2 // Make A2 a pointer again
    adda.l (%a4),%a3 // Make A3 a pointer again
    move.l %d1,RHndl(%a2) // and tell RMGR resource is loaded in.
    move.l %d1,%a0 // get handle into A0 to setup purge/lock bits
    move.b RAttr(%a2),%d1 // get attrs into D1 to setup purge/lock bits
    bsr SetPL // setup purge/lock bits
    move.l (%sp)+,theZone // restore theZone

.L_vSuperLoad_7:
    bsr NextEntry // go get the next entry to clear handle
    bpl.w .L_vSuperLoad_1 // if more entries, loop back

    move.l %d7,%a0 // get handle to InBuff into A0
    _disposhandle // free up the memory
.L_vSuperLoad_8:
    moveq #0,%d0 // set completion flag (CC=EQ)
    rts // ... and return no error

.L_vSuperLoad_9:
    move.l (%sp)+,theZone // restore theZone
.L_vSuperLoad_5:
    move.l %d7,%a0 // get handle to InBuff into A0
    _disposhandle // free up the memory
    bra NotFound // return with error.

SetupInBuff:

    move.l %d7,%a0 // get handle to Input Buffer (buff may have moved)
    move.l (%a0),%a0 // dereference it
    adda.l %d2,%a0 // add in offset (D2 can be odd, Byte stuff follows)
OddSize:
 // added new entry for UpdateResFile 			<07jun85> BBM
    move.b (%a0)+,%d0 // X.X.X.0 - We want '0.1.2.3' byte order		<09may85> BBM
    lsl.w #8,%d0 // X.X.0.X
    move.b (%a0)+,%d0 // X.X.0.1
    swap %d0 // 0.1.X.X
    move.b (%a0)+,%d0 // 0.1.X.2
    lsl.w #8,%d0 // 0.1.2.X
    move.b (%a0)+,%d0 // 0.1.2.3 - Size=D0, rsrc=A0
    rts //

//_______________________________________________________________________________
//
// Routine:		GetMaxLoad (internal)
//
// Arguments:	D0		(output.L)		-1 on error;  size of data on good rts
//				D7		(output.L)		Handle to input buffer
//
// Unchanged:	A1-A7,D3-D6
// Trashed:		A0,D0-D2
//
// Called By:	SuperLoad
// Calls:		_DisposHandle, _MaxBlock, _NewHandle, NotFound, RdData
//
// Function: 	Four step process.	First, find out how much space is available
//				in the application zone without moving anything, or purging
//				anything.  Second, find out size of resources to be read in.
//				Third, get a NewHandle of twice the size to get us some room to
//				work in, and toss it away. (This forces the heap to grow as much
//				as we will need.)  Fourth, get a NewHandle of the size we need
//				and read in data.  Return with CC=MI or D7=handle of input buff.
//_______________________________________________________________________________

GetMaxLoad:
 // READ IN MAX AMOUNT TO DISECT IN SUPERLOAD 	<24jun85> BBM
    tst.l HSCache // is there a valid key?
    beq .L_GetMaxLoad_9 // wrong format for superload 					<SM25> CSS
    moveq #0,%d7 // D7 is the amount we can grow heap.
    move.l %sp,%d0 // Get top of Stack.
    _stripaddress // Want clean arithmetic.			<C169>
    sub.l MinStack,%d0 // Allow some stack margin.
    cmp.l ApplLimit,%d0 // if ApplLimit is greater then use ...
    bcs.w .L_GetMaxLoad_1 // ... use ApplLimit instead.
    move.l ApplLimit,%d0 // D0 now has the maximum limit
.L_GetMaxLoad_1:
    sub.l HeapEnd,%d0 // if D0 <= HeapEnd
    ble.w .L_GetMaxLoad_2 // ... then can't grow heap
    cmp.l #MinFree,%d0 // can the zone grow by the minimum size
    bls.w .L_GetMaxLoad_2 // ... if not, can't grow heap.
    move.l %d0,%d7 // Stuff D7 with the amount to grow heap.
.L_GetMaxLoad_2:
    _maxblock // D0 has max space after a compact.
    cmp.l %d7,%d0 // does compact or growheap get us more?
    ble.w .L_GetMaxLoad_3 // D7 already has max value,
    move.l %d0,%d7 // ... else D7 = amount we can grow heap.

.L_GetMaxLoad_3:
    move.l HSCache+PreExtra,%d0 // calc how much to read in for superload?		<13jan86> BBM
    bsr.w SizeFits // check to see if this size fits in buff		<04sep85> BBM
    bpl.w .L_GetMaxLoad_4 // CC=PL if this fits
    move.l HSCache+PreUnlock,%d0 // check if PreUnlock fits						<13jan86> BBM
    bsr.w SizeFits // check to see if this size fits in buff
    bpl.w .L_GetMaxLoad_4 // CC=PL if this fits
    move.l HSCache+PreLock,%d0 // check if Prelock fits 						<13jan86> BBM
    bsr.w SizeFits // check to see if this size fits in buff		<04sep85> BBM
    bmi.w .L_GetMaxLoad_9 // not even this fits, so exit with error

.L_GetMaxLoad_4:
    _newhandle // by allocating some room						<15may85> BBM
    bmi.w .L_GetMaxLoad_9 // if cann't even do this exit
    _disposhandle // else dispose of space to get us some room 	<15may85> BBM
    move.l %d1,%d0 // need to know size for newhandle				<07may85> BBM
    _newhandle // get some space to read in the Rsrcs
    bmi.w .L_GetMaxLoad_9 // and return to preload with error

    move.l %d1,%d0 // get size into D0 for read
    move.l %a0,%d7 // Save off handle of PreLoad input Buffer
    move.l (%a0),%d1 // set up pointer to this buffer
    move.l (%a4),%a0 // Dereference map handle to get offset
    move.l ResDataOffset(%a0),%d2 // get start position in file
    bsr RdData // massive read of data into Buff (D6 good)
    beq.w .L_GetMaxLoad_8 // if read good return
    move.l %d7,%a0 // get handle to buffer into A0
    _disposhandle // else dispose of space to get us some room 	<11jul85> BBM

.L_GetMaxLoad_9:
    moveq #-1,%d0 // ELSE ERROR RETURN
.L_GetMaxLoad_8:
    rts //

SizeFits:
 // new routine									<04sep85> BBM
    move.l %d0,HSCache // save off for SuperLoad
    beq NotFound // if nothing to load, set CC's to MI
    bmi.w .L_SizeFits_9 // If CC=MI, just return
    move.l %d0,%d1 // Save off for later
    add.l %d0,%d0 // double D0
    move.l (%a4),%a0 // Dereference map handle
    add.l MapSize(%a0),%d0 // add in size of map
    cmp.l %d0,%d7 // Set CC's and return
.L_SizeFits_9:
    rts //

//_______________________________________________________________________________
//
// Routine:		NewMap (Utility)
//
// Arguments:	A1		(input.L)		pointer to filename
//				A1		(output.L)		pointer to map
//				A2		(output.L)		handle to map
//				D0		(output.L)		result code
//				D6		(output.L)		file reference number
//
// Called by:	InitResources, OpenResFile
// Calls:		_Close, _DisposHandle, _GetEOF, _NewHandle, ORFCommon, RdData,
//				SetFileIO, _SetHandlesize
//
// Function: 	Create a new resource map in the RMGR map chain for the given
//				resource file.	Sets up regs A1/A2/D0/D6 with values for input
//				filename.  Three possible returns:
//				D0 = 0; D6 = MRefNum	Map already exists
//				D0 = 1; D6 = refnum 	New map created
//				D0 = D6 = -1			Error return
//_______________________________________________________________________________

NewMap:

    move.l jNewMap,%a0 // get the vector from the OSJumptable			<27oct85> BBM
    jmp (%a0) // and go to the routine 						<27oct85> BBM
vNewMap:
 // Def to build dispatch entry					<27oct85> BBM
    clr.l HSCache // invalidate cache always						<20jun85> BBM
    clr.l HSCHandle // invalidate cache
    moveq #HdrSize+PreSize,%d0 // Set length = header size						<11jul85> BBM
    _newhandle
    move.w %d0, ResErr // return the error code in ResErr				<16apr85> BBM
    bmi NMNoHandle // If failed this little newHandle, exit,
    move.l %a0,%a4 // else save handle in A4						<05sep85> BBM
    bsr SetFileIODirID // Set up the file IO frame A0 -->
    bsr ORFCommon // Open the resource file with name in A1 <SM9> <PN>
    beq.w .L_vNewMap_1 // If open was successful, go on

    cmp.w #OpWrErr, %d0 // Already open?
    bne NMNoFile // If not open, can't handle, else

    movem.l %a2-%a4/%d6,-(%sp) // save regs across call 						<17sep85> BBM
    bsr GetMap // check to see if map exists					<17sep85> BBM
    movem.l (%sp)+,%a2-%a4/%d6 // restore regs									<17sep85> BBM
    bne NMNoFile // If no map then exit with error				<27oct85> BBM

    clr.w ResErr // Clear the error flag if open
    move.l %a4, %a0 // Get the map handle							<05sep85> BBM
    _disposhandle // and deallocate it.  Map already exists.
// DisposHandle returns D0=0, signal already open.
    rts // D6=refnum of already opened file

// The file exists.
// Check size; if not null, load map & set up handles.

.L_vNewMap_1:
    _geteof // Get the EOF of the resource fork.
    move.w #EOFErr, ResErr // Assume EOF error (null fork)
    tst.l IOLEOF(%a0) // Is it a null fork?
    beq NMFail // If so, close and fail--no map to load.		<SM25> CSS

    moveq #HdrSize+PreSize,%d0 // byte count=HdrSize							<11jul85> BBM
    move.l (%a4), %a1 // dereference map handle to ptr.				<05sep85> BBM
    move.l %a1, %d1 // Copy to D1 for RdData
    moveq #0, %d2 // Start at beginning of file

    bsr RdData // Read data, set new ResErr result.
    bmi NMFail // If failed, return error+close file.			<SM25> CSS

    movea.l %a1,%a0 // Source for blockmove							<20jun85> BBM
    adda.l #HdrSize,%a0 // ... finish calc for source
    lea HSCache,%a1 // Destination for blockmove
    move.l #PreSize,%d0 // size for blockmove							<11jul85> BBM
    _blockmove // move the key data to the stack
    movea.l %a0,%a1 // restore A1
    suba.l #HdrSize,%a1 // ... finish restoring A1
    bsr PreFormat // check the preload format of this file			<SM25> CSS

    moveq #HdrSize-4, %d1 // Get header size - 4 in D1 					<19sep85> BBM
    move.l ResMapOffset(%a1), %d2 // Get offset to resource map,
    add.l %d1, %d2 // but skip header
    move.l MapSize(%a1), %d4 // Get mapsize
    move.l %d4, %d0 // Get map size in D0
    move.l %a4, %a0 // Get handle in A0 for size set 				<05sep85> BBM
    _sethandlesize // Set size to mapsize
    move.w %d0, ResErr // return the error code in ResErr
    bne.w NMFail // If mem full, return error+close file.

    move.l (%a4), %a1 // Dereference new map							<05sep85> BBM

// we are about to read in the extended attributes

    clr.b mInMemoryAttr(%a1) // clear run-time-only attributes 				<sm6>stb

    move.l %d4, %d0 // Get mapsize in D0 to calc bytes to read
    sub.l %d1, %d0 // MapSize-HeaderSize=bytes to read
    add.l %a1, %d1 // skip header in buffer too
    bsr RdData // Read in the bytes!
    bmi.w NMFail // if IO err, return and close file.

    tst.l HSCache // if already zero, then old format				<19sep85> BBM
    beq.w .L_vNewMap_2 // so skip check for new format					<19sep85> BBM
    move.l MapSize(%a1),HSCache // get second key (stored in mapsize)			<19sep85> BBM
    bsr.w PreFormat // check if new format							<19sep85> BBM
.L_vNewMap_2:
    move.l %d4,MapSize(%a1) // restore true map size 						<19sep85> BBM

    bsr CheckMap // is the map in a consistent state				<29apr85> BBM
    bmi.w NMFail // ... if not, return and close file.

// Clear the overrideNextMapBit, twoDeepBit, and dontCountOrIndexDuplicatesBit
// Clear the preventMapFromClosingBit as well. These bits should be cleared when
// a new map is first created and only set after the appropriate Resource overrides
// calls have been made.

    andi.b #kAllButOverrideAttributesMask,mInMemoryAttr(%a1)

// Successful load.	Return with D6=refnum of file and D0=1 for "newmap" flag

    clr.l MNext(%a1) // Zero nextMap handle
    move.w %d6, MRefNum(%a1) // Save refnum of file in map
    moveq #1, %d0 // Return 1 for "new resource" flag
    rts // ...and return, successful open.

// error results: close the opened file, if necessary, dispose of the map
// handle if necessary, and return with D6=D0=-1.

NMFail:

    bsr SetFileIO // Set up file frame again.	D3=iorefnum.
    _close // Close the file
NMNoFile:

    move.l %a4, %a0 // Get the map handle							<05sep85> BBM
    _disposhandle // ...and dispose of it.
NMNoHandle:

    clr.l HSCache // invalidate superpreload						<20jun85> BBM
    clr.l HSCHandle // invalidate cache								<20jun85> BBM
    moveq #-1, %d6 // set D6=-1 for failure
    moveq #-1, %d0 // Return -1 for failed open flag
    rts // return to InitResources or OpenResource.

//_______________________________________________________________________________
//
// Routine:		PreFormat (Utility)
//
// Arguments:	HSCache (io.L)			input = value to test; output = zero or key
//
// Preserved:	A0-A7,D1-D7
// Trashes:		D0
//
// Called by:	NewMap
// Calls:		NONE
//
// Function: 	Check to see if the key info agrees with the map header info.			<21jun85> BBM
//				This is done by checking if HSCache=$20B7A185.	If not, clear
//				HSCache, as that invalidates superpreload.
//_______________________________________________________________________________

PreFormat:

    move.l HSCache,%d0 // get the current key							<21jun85> BBM
    sub.l #PreKey,%d0 // check to see if key is present
    beq.w .L_PreFormat_9 // key valid, jump to return
    clr.l HSCache // invalidate superpreload
.L_PreFormat_9:
    clr.l HSCHandle // invalidate cache and clear out version#		<11jul85> BBM
    rts

//_______________________________________________________________________________
//
// Routine:		PROCEDURE RsrcZoneInit;
//
// Arguments:	NONE
//
// Called By:	A-trap through Dispatcher
// Calls:		StdEntry, CloseFiles, GetEntries, StdZeroExit
//
// Function: 	Zeroes all handles in the system map which point to the
//				application zone, and closes all other maps.  This routine is
//				called from the Memory Manager when an application zone is
//				initialized.
//_______________________________________________________________________________

RsrcZoneInit:
    /* Debug: trace entry */
    lea .Ldbg_rzi(%pc),%a0
    moveq #2,%d0
    .word 0xA0FE
    bra.w .Ldbg_rzi_end
.Ldbg_rzi: .asciz "RM:RsrcZoneInit"
    .even
.Ldbg_rzi_end:

    bsr StdZEntry // Link A6, save regs							<09jul85> BBM
    tst.w SysMap // Is there at least the system map?
    bmi.w .L_RsrcZoneInit_9 // If not, just return, otherwise

    movem.l %a0-%a2/%d0-%d2,-(%sp) // help the compiler out a litle						<SM58>
    move.l ApplZone,%a0 // the zone in question
    cmp.l bufPtr,%a0 // Does this zonePtr = bufPtr? (Special Boot3.a case)<SM59>
    beq.w .L_RsrcZoneInit_noTrash // -> Yes, do not call TrashZone on the bogus zone	<SM59>
    /* _FigmentDispatch ($A0A4) TrashZone — not installed on SE, skip it.
       move.w #10,%d0
       .word 0xa0a4 */
.L_RsrcZoneInit_noTrash:
    movem.l (%sp)+,%a0-%a2/%d0-%d2

    bsr FlushResourceCache // <SM11> CSS Flush the cache first

// Now make the system map forget about resources it has loaded into the
// application heap by clearing their handle locations.

    bsr.w CloseFiles // Close all res files except system.

    move.l OverrideSysMapHndl(%a6),%d0 // Start with the override map

.L_RsrcZoneInit_mapLoop:

    move.l %d0,%a4
    jsr GetRsrcCnt // Skip to the entries							<19jul85> BBM
    bmi.w .L_RsrcZoneInit_nextMap // If no entries, no handles to zero.

    move.l SysZone,%a0 // <32>
    move.l bkLim(%a0),%d7 // <32> Keep upper limit of System heap
    move.l %a0,%d6 // <32> Keep lower limit of System heap
.L_RsrcZoneInit_unloadLoop:

    move.l RHndl(%a2),%a0 // <22> Get resource handle
    cmp.l %a0,%d6 // <32> Compare against lower bound of System zone
    bgt.w .L_RsrcZoneInit_clearReference // <32> If d6 > a0, handle is not in heap, so clear it
    cmp.l %a0,%d7 // <32> Compare resource against upper limit of System zone
    bgt.w .L_RsrcZoneInit_entryInSysHeap // <32> If d7 > a0, handle is in System heap, so don‚Äôt clear the reference out.
.L_RsrcZoneInit_clearReference:

    clr.l RHndl(%a2) // Forget the disposed handle in app. heap.
.L_RsrcZoneInit_entryInSysHeap:

    add.w #RESize,%a2 // Otherwise skip to the next entry				<19jul85> BBM
    dbra %d4,.L_RsrcZoneInit_unloadLoop // If there is another entry, loop back
.L_RsrcZoneInit_nextMap:

    move.l (%a4),%a4
    move.l mNext(%a4),%d0 // Get the next map
    bne.w .L_RsrcZoneInit_mapLoop
.L_RsrcZoneInit_9:

    bra StdZeroExit // ...and exit.

//_______________________________________________________________________________
//
// Routine:		CloseFiles (Utility)
//
// Arguments:	None?
//
// Called by:	RsrcZoneInit, CloseResFiles
// Calls:		_CloseResFile
//
// Function: 	Closes all maps except the system [override] map.
//_______________________________________________________________________________

CloseFiles:

    movem.l %a0-%a1/%d0-%d2,-(%sp) // save reg's cause GetOverrideMap destroys d1 & d2
.L_CloseFiles_1:
    move.l TopMapHndl, %a0 // Get the top map
    cmpa.l OverrideSysMapHndl(%a6), %a0 // Is the current map the SYS map? <SM11> CSS
    beq.w .L_CloseFiles_9 // if so, just exit
// eliminated special checks for rom map here	<22jul85> BBM
    move.l (%a0), %a0 // Dereference map handle
    move.w MRefNum(%a0), -(%sp) // Push the map refnum
    _closeresfile // Close the resource file.
    bra.w .L_CloseFiles_1 // and loop back. <SM11> CSS
.L_CloseFiles_9:
    movem.l (%sp)+,%a0-%a1/%d0-%d2
    rts //

//_______________________________________________________________________________
//
// Routine:		ORFCommon (Utility)
//
// Arguments:	A1		(input.L)		pointer to resource file name
//				A0		(input.L)		pointer to the I/O block
//				D0		(output.W)		Error code
//				D6		(output.W)		IORefnum of File
//
// Called by:	NewMap, CreateResFile
// Calls:		_OpenRF
//
// Function: 	Open the resource fork whose name is in A1.  A0 must point to
//				the I/O block, usually allocated by SetFileIO.	Returns Error
//				code in  D0/ResErr, and IORefNum in D6.
//
//				First check to see if it is coming from the old .print driver.
//				If it is, set the IOVRefNum to the BootDrive.
//_______________________________________________________________________________

ORFCommon:
 // rewritten for OpenRFPerm						<05sep85> BBM

// This old code checked for the .Print driver open case by turning the pointer into a
// handle with RecoverHandle and subsequently calling GetResInfo to see if it was STR
// $E000. This causes a crash when passing pointers to strings in resouce handles. To
// avoid this, the new code checks for the .Print driver by getting the STR $E000
// resource (with SetResLoad(false)) and comparing it with the file name pointer that
// was passed.

    move.l %a0,-(%sp) // save pointer to parameter block

    move.w CurMap,-(%sp) // save the current map
    move.b ResLoad,-(%sp) // save the old value for whether to load resources

    move.w OverrideSysMap(%a6),CurMap // get the resource from the system file
    sf ResLoad // don‚Äôt load the resource

    subq #4,%sp // make room for resource handle
    move.w #0xE000,-(%sp) // get that resource handle
    _getstring // got the string now!
    move.l (%sp)+,%d0 // get the string into a register

    move.b (%sp)+,ResLoad // restore ResLoad
    move.w (%sp)+,CurMap // restore CurMap

    move.l (%sp)+,%a0 // get back the parameter block pointer

    tst.l %d0 // check if the handle is nil
    beq.w .L_ORFCommon_notPrint // nope, it is not the printer driver name
    exg %d0,%a1 // move to address register for dereference
    move.l (%a1),%a1 // dereference to get the pointer
    exg %d0,%a1 // move file name pointer back to A1
    cmp.l %d0,%a1 // check and see if it is the printer name
    bne.w .L_ORFCommon_notPrint // nope, it is not the printer driver

// Note that we can get here if there is a STR $E000 that has not yet been
// loaded and the file name pointer in A1 is NIL! This is not a problem,
// since a NIL file name pointer is illegal anyway, and changing the vRefNum
// in the parameter block for OpenRF shouldn‚Äôt have any effect.

    move.w BootDrive,ioVRefNum(%a0) // always open the printer driver on the boot drive
.L_ORFCommon_notPrint:



    move.l %a1,IOFileName(%a0) // Set filename pointer
    clr.l IOOwnBuf(%a0) // No buffer
    _OpenRF // HFSOpen the file's resource fork <SM9> <PN>
    move.w IORefNum(%a0),%d6 // set D6=file refnum.
    move.w %d0, ResErr // and set ResErr
    rts

//_______________________________________________________________________________
//
// Routine:		PROCEDURE HCreateResFile(vRefNum: INTEGER; dirID: LONGINT; fileName: Str255); <SM9> <PN>
//
// Called By:	A-trap through dispatcher.
//
// Function:
//				Initialize a null resource fork for the given filename
//				In the specified directory and volume.
//
// From ResourceMgrPatches.a
//_______________________________________________________________________________
//
// RECORD MyStkFrame 0
    .space (1)*4
    .space (1)*4
    .space (1)*4
    .space (1)*4
    .space (1)*2
// ENDR

HCreateResFile:

// WITH MyStkFrame
    bsr StdZEntry // Link and save regs

    move.l 0x00(%a0), %a1 // Get filename pointer

    bsr SetFileIODirID // Set up the file IO frame A0 -->

    move.w vRefNum(%a6),ioVRefNum(%a0) // stuff vRefNum into parameter block <SM9> <PN>
    move.l dirID(%a6),ioDirID(%a0) // stuff the ioDirID into parameter block <SM9> <PN>

// ENDWITH

    bsr.w ORFCommon // Attempt to open the resource fork. <SM9> <PN>
// D6 := refnum of file.
    beq.w .L_HCreateResFile_1 // If successful, see if the fork is null.
    cmp.w #FNFErr, %d0 // Was the error "file not found"?
    bne.w .L_HCreateResFile_9 // If a different error, can't handle, else

//Roll in NewCreate from ResourceMgrExtensions.a

    _Create // HFScreate a new file with null D+R forks <SM9> <PN>

    move.w %d0,ResErr // ... else (A0) points to parameter block		<27feb85> BBM
    bmi.w .L_HCreateResFile_9 // ... and error exit

    bsr.w ORFCommon // and attempt to open again. D6=refnum.<SM9> <PN>
    bmi.w .L_HCreateResFile_9 // If open failed, can't do anything.

.L_HCreateResFile_1:
    _geteof // Get EOF of rsrc fork. (file exists!)
    move.w #DupFNErr, ResErr // Assume error, fork already exists
    tst.l IOLEOF(%a0) // Is it a null fork, ready to initialize?
    bne.w .L_HCreateResFile_7 // If not, leave DupFNErr and exit, else

    moveq #HdrSize, %d0 // Write header only at 0.
    lea NullRF, %a1
    move.l %a1, %d1 // Set buffer address to null map
    bsr WrDataSt // Write out initial map @0.  Set ResErr.

    moveq #NullRMSize, %d0 // Set number of bytes
    moveq #64, %d2 // Get 64 for offset...
    asl.w #2, %d2 // left shift 2 for 256=FirstResData.
    bsr WrData // Write out initial map.  Set ResErr.
.L_HCreateResFile_7:
    _close // Close the file.  Error code in D0 ignored...
// WrData, WrDataSt preserved A0.

.L_HCreateResFile_9:
    jmp StdTenExit // clear the dirID and exit (done by StdExit on newer ROMs) <SM9> <PN>

//_______________________________________________________________________________
//
// Routine:		PROCEDURE CreateResFile(fileName:  Str255);
//
// Arguments:	8(A6), A0				pointer to filename
//
// Called By:	A-trap through dispatcher.
// Calls:		StdEntry, SetFileIO, ORFCommon, _Create, _GetEOF, WrDataSt,
//				WrData, _Close, StdFourExit
// Function:
//				Initialize a null resource fork for the given filename.
//				If the file does not exist at all, create it.
// note:
// <27feb85> BBM added check to see if file
//_______________________________________________________________________________

CreateResFile:

    bsr StdZEntry // Link and save regs							<09jul85> BBM

    move.l (%a0), %a1 // Get filename pointer
    bsr SetFileIODirID // Set up the file IO frame A0 -->
    bsr.w ORFCommon // Attempt to open the resource fork.
// D6 := refnum of file.
    beq.w .L_CreateResFile_1 // If successful, see if the fork is null.
    cmp.w #FNFErr, %d0 // Was the error "file not found"?
    bne.w .L_CreateResFile_9 // If a different error, can't handle, else
    _create // create a new file with null D+R forks

    move.w %d0,ResErr // ... else (A0) points to parameter block		<27feb85> BBM
    bmi.w .L_CreateResFile_9 // ... and error exit

    bsr.w ORFCommon // and attempt to open again. D6=refnum.
    bmi.w .L_CreateResFile_9 // If open failed, can't do anything.

.L_CreateResFile_1:
    _geteof // Get EOF of rsrc fork. (file exists!)
    move.w #DupFNErr, ResErr // Assume error, fork already exists
    tst.l IOLEOF(%a0) // Is it a null fork, ready to initialize?
    bne.w .L_CreateResFile_7 // If not, leave DupFNErr and exit, else

    moveq #HdrSize, %d0 // Write header only at 0.
    lea NullRF, %a1
    move.l %a1, %d1 // Set buffer address to null map
    bsr WrDataSt // Write out initial map @0.  Set ResErr.

    moveq #NullRMSize, %d0 // Set number of bytes
    moveq #64, %d2 // Get 64 for offset...
    asl.w #2, %d2 // left shift 2 for 256=FirstResData.
    bsr WrData // Write out initial map.  Set ResErr.
.L_CreateResFile_7:
    _close // Close the file.  Error code in D0 ignored...
// WrData, WrDataSt preserved A0.
.L_CreateResFile_9:
    bra StdFourExit // pop 4 bytes and exit


//_______________________________________________________________________________
//
// Routine:		FUNCTION HOpenResFile(vRefNum: INTEGER; dirID: LONGINT; fileName:
//												Str255; permission: SignedByte): INTEGER;
//
// Function: 	Given filename, opens this resource.  Returns resource file
//				reference number.  If the file was already open, returns the
//				current refnum of the file.  If negative, an error occurred and
//				the code can be noted in ResErr.  InitResources must be called
//				before OpenResFile.  Sets ResLoad to TRUE initially.
// From ResourceMgrPatches.a
//_______________________________________________________________________________
// WITH ResourceMgrStack

HOpenResFile:


    bsr StdZEntry // Link and save regs

    move.w 0x0a(%a0),ioBlock+IOVRefNum(%a6) // stuff vRefNum into ioParmBlk
    move.l 0x06(%a0),ioBlock+ioDirID(%a6) // Stuff dirID into ioParmBlk
    move.l 0x02(%a0),%a1 // Get fileName in A1 for call to NewMap
    move.b 0x00(%a0),ioBlock+ioPermssn(%a6) // set up permission byte <SM9> <PN>
    bsr OPResFileCommon // go open the file.
    move.w %d6,20(%a6) // output refNum‚Ä¶
.L_HOpenResFile_9:
    bra StdTwelveExit // clear the dirID and exit (done by StdExit on newer ROMs)<SM9> <PN>

// ENDWITH


//_______________________________________________________________________________
//
// Routine:		FUNCTION OpenResFile(fileName:	Str255): INTEGER;
//
// Arguments:	8(A6),A0		(input) 		pointer to filename
//				12(A6)			(output.x)		Resource file refnum.
//
// Called By:	(OpenResFile): A-trap through Dispatcher
//				(OPResFile): OpenRFPerm
// Calls:		StdEntry, NewMap, PreLoad2, StdFourExit
//
// Function: 	Given filename, opens this resource.  Returns resource file
//				reference number.  If the file was already open, returns the
//				current refnum of the file.  If negative, an error occurred and
//				the code can be noted in ResErr.  InitResources must be called
//				before OpenResFile.  Sets ResLoad to TRUE initially.
// note:
// <22Jan85> BBM We want to call PreLoad, but we don't want to set ResLoad flag,
//				so we call PreLoad2.  This fix was part of the ram patches to
//				the version 7.0 ROM.
//_______________________________________________________________________________
OpenResFile:

//‚Ä¢‚Ä¢‚Ä¢‚Ä¢ Fix for the old AppleTalk ImageWriter and LQ AppleTalk  ImageWriter until these drivers are fixed <SM35>
    move.b ResLoad,-(%sp) // save the old value for whether to load resources <SM35>
    sf ResLoad // don‚Äôt load the resource <SM35>

    subq #4,%sp // make room for resource handle <SM35>
    move.w #0xE000,-(%sp) // get that resource handle <SM35>
    _getstring // got the string now! <SM35>
    move.l (%sp)+,%d0 // get the string into a register <SM35>

    move.b (%sp)+,ResLoad // restore ResLoad <SM35>

    tst.l %d0 // check if the handle is nil <SM35>
    beq.w NotForPrinting // nope, it is not the printer driver name <SM35>
    exg %d0,%a0 // move to address register for dereference <SM35>
    move.l (%a0),%a0 // dereference to get the pointer <SM35>
    exg %d0,%a0 // move file name pointer back to A1 <SM35>
    cmp.l 4(%sp), %d0 // check and see if it is the printer name <SM35>
    bne.w NotForPrinting // nope, it is not the printer driver <SM35>

    movem.l %d1-%d2/%a1,-(%sp) // Resource Mgr calls shouldn‚Äôt touch these	<SM35>
    move.l #0xD8000000,-(%sp) // PrLoadDriver <SM35>
    _prglue // Call PrGlue <SM53> CSS
    movem.l (%sp)+,%d1-%d2/%a1 // restore registers	<SM35>										<4>
    move.w %d0, ResErr // copy the error result into ResErr <SM35>
    move.w 0x952, %d0 // copy the printing refnum into the function result <SM35>
// This will be the error from HOpenResFile if it didn't work. <SM35>

    move.l (%sp)+, %a0 // save return address <SM35>
    add.l #4, %sp // strip the parameter <SM35>
    move.w %d0, (%sp) // set the function result <SM35>
    jmp (%a0) // out of here <SM35>

NotForPrinting:
 // <SM35>
//‚Ä¢‚Ä¢‚Ä¢ end fix
    bsr StdZEntry // Link A6, save regs							<30oct85> BBM

    move.l (%a0), %a1 // Get name pointer in A1

    bsr OPResFileCommon // go open the file.
    move.w %d6,12(%a6) // output refNum‚Ä¶
    bra StdFourExit // ...and exit

//_______________________________________________________________________________
//
// Routine:		FUNCTION OpenRFPerm(fileName:Str255, VRefNum: INTEGER,
//								Permission: Byte): INTEGER;
// Arguments:	????????
//
// Called By:	A-trap through Dispatcher
// Calls:		StdZEntry, OPResFile
//
// Function: 	Given filename, volume reference number and permissions, open
//				this resource.	Returns resource file reference number.  If the
//				file was already open, returns the current refnum of the file.
//				If negative, an error occurred and the code can be noted in
//				ResErr.  InitResources must be called before OpenResFile.
//_______________________________________________________________________________
ROMOpenRFPerm:

OpenRFPerm:
 // new routine
    bsr StdZEntry // Save regs etc. (D0 saved across call)

// WITH ResourceMgrStack
    move.w 0x02(%a0),ioBlock+IOVRefNum(%a6) // stuff vRefNum into ioParmBlk
    move.l 0x04(%a0),%a1 // Get fileName in A1 for call to NewMap
    move.b 0x00(%a0),ioBlock+ioPermssn(%a6) // set up permission byte <SM9> <PN>

    bsr OPResFileCommon // go open the file.
// ENDWITH

    move.w %d6,16(%a6) // output refNum‚Ä¶
    bra StdEightExit // and go to common code 						<05sep85> BBM


//_______________________________________________________________________________
//
// Routine:		OPResFileCommon
// Arguments:	A1 - Ptr to file name of resource fork to open‚Ä¶
//
// Called By:	HOpenResFile, OpenResFile, and OpenRFPerm
// Calls:		NewMap, PreLoad2
//
// A1 points to name to open at this point and permissions and DirID are set
// in the pb on the stack.
//
// return refNum in D6

OPResFileCommon:


    move.l MinusOne,LastSPExtra // invalidate font cache

    bsr NewMap // D0=dirID, D3=refnum, A1=map ptr, A2 is map handle
    move.w %d6,-(%sp) // Return refnum of map, or -1 for error. (save refNum on the stack)
    bmi.w .L_OPResFileCommon_9 // If I/O error, return.
    move.w %d6, curMap // Save it as current map.
    tst.w %d0 // Was it a newly-created map?
    beq.w .L_OPResFileCommon_9 // No, it was already there, don't relink.
    move.l TopMapHndl, MNext(%a1) // If new, link. Store old top as next map
    move.l %a4,TopMapHndl // Set new map as top map						<05sep85> BBM
    bsr PreLoad2 // Preload required resources... 				<22Jan85> BBM
    bsr CheckAndFlushFontCache // If this new file has fonts we should flush the font cache‚Ä¶
.L_OPResFileCommon_9:

    move.w (%sp)+,%d6 // get the ref num to return
    rts // exit

//_______________________________________________________________________________
//
// Routine:		PROCEDURE	UseResFile(RefNum: INTEGER);
//
// Arguments:	4(SP)	(input) 		resource file refnum
//
// Called By:	A-trap through dispatcher
// Calls:		StdMapEntry, StdTwoExit
//
// Function: 	Given refnum, sets current resource to use.  If there isn't any
//				such map, ignores the request.
//_______________________________________________________________________________

UseResFile:

// We have to check for UseResFile(0) first because this is the one case where
// we don't want the override of the system to come back in d6 but instead we want
// to have the real SysMap in d6.

    bsr StdZEntry // Link and save regs							<12aug85> BBM
    move.w (%a0), %d6 // Get the resource map refnum
    bne.w .L_UseResFile_notSystemMap

    move.w SysMap,%d6 // 0 means use SysMap
    bra.w .L_UseResFile_exit

.L_UseResFile_notSystemMap:

    bsr GetMap // Scan for it
    bmi.w .L_UseResFile_err // If not found, exit, otherwise

.L_UseResFile_exit:

    move.w %d6, CurMap // Set this refnum as the current map
    clr.w doOverrides(%a6) // Turn off overrides so that the CurMap doesn't get overwritten. <SM49> CSS
    bra StdTwoExit // and exit.

.L_UseResFile_err:

    move.w #ResFNotFound, ResErr // set error code, and
    bra StdTwoExit // and exit.



//_______________________________________________________________________________
//
// Routine:		FUNCTION GetResFileAttrs(refnum:  INTEGER): INTEGER;
//
// Arguments:	8(A6)	(input.W)		Resource file refnum
//				10(A6)	(output.B)		Attribute byte
//
// Called By:	A-trap through dispatcher
// Calls:		StdMapEntry, StdTwoExit
//
// Function: 	Given a resource file, return its attribute byte in low order.
//				high order is trash.
//_______________________________________________________________________________

GetResFileAttrs:

    bsr.w StdMapEntry // Look for the file.  If not there, exit
    move.b MAttr(%a3),11(%a6) // Return the byte in low order
    bra StdTwoExit // and exit.

//_______________________________________________________________________________
//
// Routine:		PROCEDURE SetResFileAttrs(refnum:  INTEGER; attrs:	 INTEGER);
//
// Arguments:	10(A6)	(input.W)		resource file refnum
//				8(A6)	(input.W)		Resource attribute byte)
//
// Called By:	A-trap through dispatcher
// Calls:		StdEntry, GetMap, StdFourExit
//
// Function: 	Given a resource file, set its attribute byte.
// note:
//				there is no error checking!!  The attribute bytes could be wrong
//				and this procedure will still write them out.
//_______________________________________________________________________________

SetResFileAttrs:

    bsr StdZEntry // Look for the file.  If not there, exit		<12aug85> BBM
    move.w 10(%a6), %d6 // Get the refnum
    bsr GetMap // Get the corresponding map
    bmi.w .L_SetResFileAttrs_1 // if can't find, just exit.
    move.b 9(%a6), MAttr(%a3) // Set attributes word
.L_SetResFileAttrs_1:
    bra StdFourExit // and exit.

//_______________________________________________________________________________
//
// Routine:		StdMapEntry (utility)
//
// Arguments:	8(A6)	(input.W)		Map refnum
//				D6		(output.W)		Map refnum
//
// Called By:	UseResFile, UpdateResFile, CloseResFile
// Calls:		StdEntry, GetMap, StdTwoExit
//
// Function: 	Given the map refnum 8(A6), scans for the map and exits if not
//				found.	StdEntry returns A0=8(A6), so the refnum is copied from
//				(A0).  Callers must have a two-byte argument.
//_______________________________________________________________________________

StdMapEntry:

    move.l (%sp)+, %d0 // Save return address in D0
    bsr StdZEntry // Link and save regs							<12aug85> BBM
    move.l %d0, -(%sp) // Restore return address
    move.w (%a0), %d6 // Get the resource map refnum
    bsr GetMap // Scan for it
    bmi.w .L_StdMapEntry_1 // If not found, exit, otherwise
    rts // just return.

.L_StdMapEntry_1:
    addq #4, %sp // pop return address,
    move.w #ResFNotFound, ResErr // set error code, and
    bra StdTwoExit // and exit.

//_______________________________________________________________________________
//
// Routine:		FirstRes, NextRes, ZfirstRes, ZnextRes (utilities)
//
// Arguments:	A2		(input.L)		Pointer to last rsrc (used by NextRes)
//				A4		(input.L)		map handle
//				D1		(input.L)		which bit to test in RAttr(A2)
//				A2		(output.L)		Pointer to the resource entry
//				D0		(output.L)		Next file Location
//
// Unchanged:	A0-A1,A3-A7,D1-D7
// Trashed:		None
//
// Called By:	(FirstRes): PreLoad, PreLoad2,UpdateResFile
//				(NextRes): PreLoad, PreLoad2,UpdateResFile
// Calls:		GetRsrcCnt
//
// Function: 	FirstRes and NextRes¬†are File-Order routines.  They are used to
//				find the first/next resource in file order.  D1 determines what
//				kind of search to do.  If (D1.W < 0) then FirstRes/NextRes will
//				find the first/next resource in file order.  If (D1.W > 0) then
//				FirstRes/NextRes will test RAttr(A2) with bit referenced in D1.
//				In other words if (D1.W > 0) then FirstRes/NextRes will find
//				the first/next resource in file order with that bit set.
//				FirstRes/NextRes point A2 to the entry found and sets CC=EQ.  If
//				no entry was found A2 is garbage, and CC=MI.  Alternate entries
//				ZfirstRes and ZnextRes force search through all rsrcs.
//_______________________________________________________________________________

ZfirstRes:

    moveq #-1,%d1 // get the first resource in file order			<03oct89 bpa>
FirstRes:

    moveq #-1, %d0 // prime with -1 for first resource
    bra.w FNCmn // and jump to common code
ZnextRes:

    moveq #-1,%d1 // get the very next resource					<03oct89 bpa>
NextRes:

    move.l RLocn(%a2), %d0 // Get the location
    and.l Lo3Bytes,%d0 // and mask off the high byte.
FNCmn:
 // (only FirstRes uses this label)
    movem.l %a3/%d2-%d7,-(%sp) // Save regs
    move.l %d0, %d2 // Copy location to find successor of
    bsr GetRsrcCnt // Point to the first entry						<10may85> BBM
    bmi.w .L_FNCmn_9 // If none, exit
    lea MinusOne,%a3 // Point to MinusOne, but						<26aug85> BBM
    subq.l #RLocn,%a3 // skip back RLocn. (RLocn(A2)=MinusOne).
    move.l %a3,%d6 // D6=new candidate.  valid A2 will replace.
    moveq #-1, %d7 // D7:=FFFFFFFF
    lsr.l #1, %d7 // shift rt by 1 = 7FFFFFFF, +ve infinity.
    tst.w %d1 // if (D1 < 0) then get next in file order 		<03oct89 bpa>
    bmi.w .L_FNCmn_3 // ... else fall through to test bit loop		<03oct89 bpa>

.L_FNCmn_1:
    btst %d1,RAttr(%a2) // if you don't want to preload this one,        <10may85> BBM
    beq.w .L_FNCmn_2 // ... then don't look at it.
    move.l RLocn(%a2), %d0 // Get the resource location
    and.l Lo3Bytes,%d0 // AND off high order 8 bits
    cmp.l %d0, %d2 // is this location greater than given?
    bge.w .L_FNCmn_2 // No, try next
    cmp.l %d0, %d7 // is it less than the last candidate?
    ble.w .L_FNCmn_2 // no, old candidate is still good
    move.l %d0, %d7 // else replace old candidate with new
    move.l %a2, %d6 // Save A2 candidate also in D6
.L_FNCmn_2:
    add.w #RESize,%a2 // point to the next entry
    dbra %d4,.L_FNCmn_1 // if there was one, loop back
    bra.w .L_FNCmn_8 // ... else goto exit code.

.L_FNCmn_3:
    move.l RLocn(%a2), %d0 // Get the resource location 					<13may85> BBM
    and.l Lo3Bytes,%d0 // AND off high order 8 bits
    cmp.l %d0, %d2 // is this location greater than given?
    bge.w .L_FNCmn_4 // No, try next
    cmp.l %d0, %d7 // is it less than the last candidate?
    ble.w .L_FNCmn_4 // no, old candidate is still good
    move.l %d0, %d7 // else replace old candidate with new
    move.l %a2, %d6 // Save A2 candidate also in D6
.L_FNCmn_4:
    add.w #RESize,%a2 // point to the next entry
    dbra %d4,.L_FNCmn_3 // if more loop back, else fall into exit code

.L_FNCmn_8:
    move.l %d6,%a2 // return A2 as the next resource entry
    move.l %d7,%d0 // Return D0 as the next file Location
    move.l RLocn(%a2),%d7 // Get this entry's location.
    and.l Lo3Bytes,%d7 // and off high bits.
    cmp.l %d0,%d7 // Set CC's.  EQ=valid entry, MI=end of list.
.L_FNCmn_9:
    movem.l (%sp)+,%a3/%d2-%d7 // Restore regs
    rts // ...and return.

//_______________________________________________________________________________
//
// Routine:		PROCEDURE UpdateResFile(refNum:  INTEGER);
//
// Arguments:	8(A6)	(input.W)		resource file refnum
//
// Called By:	A-trap through dispatcher
// Calls:		StdMapEntry, ResRW, StdTwoExit, SetRFEOF, FirstRes, WrRsrc,
//				NextRes, CheckLoad, _ReleaseResource, SizeRes, WrData, WrDataSt
//				ClearIgnore
// Labels:		UpdateResFile, UpDateCompact, WriteMap
//
// Function: 	Given refnum, writes out new resources and updates map.  If
//				refnum=0, use system file.	CloseResFile calls UpdateResFile,
//				so on exit from an application all resource files which are
//				marked dirty have thier maps writen out and are compacted.
//
//				The new UpdateResFile (July 85) uses one buffer to do all the
//				reads and writes.  When compacting the file the regester usage
//				is as follows:
//				A1		end of the region already compacted and still in buffer.
//				A2		pointer to Resource entry.
//				A3		handle to the start of locked update buffer.
//				A4		handle to resource map.
//				D3		pointer to the end of the update buffer.
//				D5		current Read position in the file less ResDataOffset.
//				D6		current resource file refnum.
//				D7		current Write position in the file less ResDataOffset.
//_______________________________________________________________________________

UpdateResFile:

// WITH ResourceMgrStack

    move.l ExpandMem,%a0 //
    move.b emScanOverrideMaps(%a0),%d0 // save override state. (StdZEntry must preserve d0)
    clr.b emScanOverrideMaps(%a0) // turn it off for StdEntry

    bsr StdZEntry // Link and save regs							<12aug85> BBM

    move.w (%a0), %d6 // Get the resource map refnum

    move.l ExpandMem,%a0 //
    move.b %d0,emScanOverrideMaps(%a0) // restore override state

    bsr GetMap // Scan for it
    bmi.w .L_UpdateResFile_ErrExit // If not found, exit, otherwise

    clr.w ResErr // start out fresh‚Ä¶
    bsr UpdateResFileCommon // first try updating this resfile‚Ä¶

    tst.w ResErr // Did we get the first file?
    bra.w .L_UpdateResFile_exit // no way‚Ä¶ get outta here!

    subq #4,%sp
    move.l %a4,-(%sp)
    _getoverridemap // See if there‚Äôs an override map for this file
    move.l (%sp)+,%a3 // Get the map

// Check to see if we‚Äôre updating the system file, or just another file.  If we‚Äôre updating the
// system file, then make sure we update all the font files too.  If it‚Äôs just a generic resource map,
// update just it‚Äôs override maps and the file itself.
//
// Note:  I know that explicitly updating an override map of the system won‚Äôt update the
// font files.  I think this is the right behaviour, since anyone that is referencing
// the override map explicitly probably has something specific in mind.

    cmp.l SysMap(%a6),%a4 // If we‚Äôre updating the system file, then update the font files too
    bne.w .L_UpdateResFile_notUpdatingSystemFile // This is done by setting a4 to nil, so that the loop won‚Äôt end until
    suba.l %a4,%a4 //  the end of the chain is reached
    bra.w .L_UpdateResFile_updateOverrideMaps

// It‚Äôs not the system file.

.L_UpdateResFile_notUpdatingSystemFile:

    cmp.l %a3,%a4 // If the file didn‚Äôt have any override maps
    beq.w .L_UpdateResFile_exit // Just update the original.

// Update any changed override maps for this file.

.L_UpdateResFile_updateOverrideMaps:

    move.l %a4,-(%sp) // save a4
    move.l %a3,%a4 // get map to try in a4
    bsr UpdateResFileCommon // Try updating this map‚Ä¶
    move.l (%sp)+,%a4 // restore a4
    move.l (%a3),%a0 // Dereference the map handle again
.L_UpdateResFile_nextMap:

    move.l mNext(%a0),%d0 // Get the next resource map handle
    cmp.l %d0,%a4 // Is the next map the original map?
    beq.w .L_UpdateResFile_exit // Yep.  End the loop and fall through to UpdateResFile for the final time
    move.l %d0,%a3 // Otherwise, update the next one if needed
    bra.w .L_UpdateResFile_updateOverrideMaps

// Update the original file
.L_UpdateResFile_ErrExit:

    move.w #ResFNotFound,ResErr // resource file not found error <PN>
.L_UpdateResFile_exit:

    bsr FlushResourceCache // from UpdateResourceOverrideFile <PN>
    bra StdTwoExit
// ENDWITH

// UpdateResFileCommon takes a map in A4 and tries to update it‚Ä¶
// it preserves a0-a4/d0-d1

UpdateResFileCommon:

    movem.l %a0-%a4/%d0-%d1,-(%sp) // save regs
    bsr ResRW // Read/write allowed for this resFile?
    beq.w .L_UpdateResFileCommon_1 // yes, go on									<16apr85> BBM
    move.w #mapReadErr,ResErr // ... else don't allow write, mark as error
    bra UpdateResExit // ... and exit always							<03jul85> BBM
.L_UpdateResFileCommon_1:
    btst #MapChanged,MAttr(%a0) // Map changed? (***Hack!, should be MapUpdate)
    beq UpdateResExit // if not don't bother to update

    bsr SetRFEOF // Check disk full even though might compact 	<28jan85> BBM
    bmi UpdateResExit // if failed return error

    bclr #MapCompact, MAttr(%a3) // Do I need to compact? 						<12jun85> BBM
    bne.w UpdateCompact // If so, skip to compact stuff

    moveq #ResChanged,%d1 // which bit to check in FirstRes/NextRes		<09apr85> BBM
    bsr FirstRes // first resource in file order that changed 	<13may85> BBM
    bmi.w .L_UpdateResFileCommon_4 // If none, go on.								<03jul85> BBM
.L_UpdateResFileCommon_0:
    bsr WrRsrc // otherwise write out the changed resource. 	<09apr85> BBM
    bsr NextRes // next in file order (NOTE D1=ResChanged still) <13may85> BBM
    beq.w .L_UpdateResFileCommon_0 // and loop back, if any left.
.L_UpdateResFileCommon_4:
    bra WriteMap // and go write map if necc. 					<12jun85> BBM

UpDateCompact:
 // this section does a buffer update 			<12jun85> BBM
    bsr ZfirstRes // get the first resource in file order			<04jun85> BBM
    bmi WriteMap // if none, go on.								<12jun85> BBM

    move.l (%a4),%a0 // Dereference map handle						<10oct85> BBM
    move.l ResDataOffset(%a0),%d7 // D7 will point to current write position		<09oct85> BBM
    sub.l ResMapOffset(%a0),%d7 // Do we need to compress this file? 			<09oct85> BBM
    bcs.w .L_UpDateCompact_2 // Compact to ResDataOffset						<10oct85> BBM
    bsr GetRsrcCnt // set up indexing for looping					<10oct85> BBM
.L_UpDateCompact_1:
    add.l %d7,RLocn(%a2) // Add in new resDataOffset						<10oct85> BBM
    add.w #RESize,%a2 // point to the next resource					<10oct85> BBM
    dbra %d4,.L_UpDateCompact_1 // loop back through entire map					<10oct85> BBM
    move.l ResMapOffset(%a0),ResDataOffset(%a0) // make new resDataOffset 			<10oct85> BBM
    bsr ZfirstRes // get the first resource in file order			<10oct85> BBM

.L_UpDateCompact_2:
    moveq #0,%d7 // compact to the beginning of ResDataOffset 	<10oct85> BBM
    bra.w stFindGap // Check to see if first rsrc needs compaction	<10oct85> BBM

findGap:

    btst #ResChanged,RAttr(%a2) // Does this resource need to be written?
    beq.w .L_findGap_1 // ... No, go get its size
    bsr WrRsrc // ... else write out resource
    move.l RHndl(%a2),%a0 // What is the size of this resource?
    _gethandlesize //
    bra.w .L_findGap_2 // go check if time to start compact
.L_findGap_1:
    bsr SizeRsrc // Get the size of this resource
.L_findGap_2:
    addq.l #LenWdLen,%d0 // add in length size to size of resource (.L)	<01jul85> BBM
    move.l RLocn(%a2),%d7 // D7 points to current position in file
    add.l %d0,%d7 // point past this resource
    and.l Lo3Bytes,%d7 // mask off RAttrs								<13jun85> BBM
    bsr ZnextRes // Get the next resource 						<02jul85> BBM
    bmi wrDtSize // maybe only map needs to be moved up			<09jan86> BBM
stFindGap:
 // new label 									<10oct85> BBM
    move.l %d7,%d0 // need a 24 bit compare of RLocn(A2) & D7		<09oct85> BBM
    sub.l RLocn(%a2),%d0 // Compare this with the next file offset
    and.l Lo3Bytes,%d0 // result is a 24 bit compare if EQ
    beq.w findGap // don't start compact yet

    sub.l (%a4),%a2 // we make A2 an offset since map may move		<29jun85> BBM

    bsr MaxRFEOF // find how much space we need to use			<09jan86> BBM
    move.l %d0,%d3 // Calculate how big this file is				<09jan86> BBM
    _maxblock // how much can we get?							<09jan86> BBM
    cmp.l %d3,%d0 // Get minimum of (filesize, maxblock) into D3	<C268>
    bge.w .L_stFindGap_0 // D3 already has the minimum					<C268>
    exg %d3,%d0 // only if D0 and D3 are backwards				<C268>
.L_stFindGap_0:

    moveq #64,%d0 // make sure we have a buffer size >= 1k 		<09jan86> BBM
    asl.w #4,%d0 // D0 = $400 (or 1K) 							<09jan86> BBM
    cmp.l %d0,%d3 // Is it greater than 1k?						<09jan86> BBM
    bge.w .L_stFindGap_1 // CC=GE means we can get buffer in Zone 		<09jan86> BBM

    move.l %d0,%d3 // D3 will be a buffer of 1024k size 			<09jan86> BBM
    link %a3,#-1024 // Get 1k buffer and set up bogus MP for A3		<08jan86> BBM
    move.l %sp,(%a3) // A3 now points to a good MP					<08jan86> BBM
    bra.w .L_stFindGap_2 // and don't try to lock down the stack          <08jan86> BBM

.L_stFindGap_1:
    move.l %d3,%d0 // D3 is size of update buffer					<09jan86> BBM
    _newhandle // Get a handle to my update buffer				<09jan86> BBM
    _hlock // Don't allow it to move and ruin my ptrs       <08jan86> BBM
    move.l %a0,%a3 // A3 is our handle to update buffer 			<A340>

.L_stFindGap_2:
    add.l (%a4),%a2 // restore A2 to a pointer						<08jan86> BBM

    move.l (%a3),%d0 // D0 now points to the beginning of buffer
    _stripaddress // make sure no garbage in high byte				<c396>
    add.l %d0,%d3 // D3 now points to end of update buffer
    move.l %d0,%a1 // A1 is our compacted region pointer
    move.l %a1,%d4 // nothing is read in yet, so D4 = A1			<01jul85> BBM
    move.l %d7,%d5 // D5 = file read pos; D7 = file write pos

FPvCmpFrm:

    move.l jCmpFrm,%a0 // get the vector from the OSJumptable			<27oct85> BBM
    jmp (%a0) // and go to the routine 						<27oct85> BBM
vCmpFrm:
 // Def to build dispatch entry					<27oct85> BBM

.L_vCmpFrm_3:
    btst #ResChanged,RAttr(%a2) // is the rsrc in mem (and needs to be written)
    beq.w .L_vCmpFrm_4 // CC=EQ means ResChanged bit not set
    bsr.w CmpFrmMem // else get rsrc from memory and compact
    bpl.w .L_vCmpFrm_5 // go get next resource							<02jul85> BBM
.L_vCmpFrm_4:
    bsr CmpFrmDsk // read rsrc from disk and compact
.L_vCmpFrm_5:
    bsr ZnextRes // go get the next resource
    beq.w .L_vCmpFrm_3 // if more resources loop back

.L_vCmpFrm_6:
    move.l %a1,%d0 // empty the buffer of the remaining stuff
    sub.l (%a3),%d0 // ... now D0 has size left to write 			<10jul85> BBM
    _stripaddress // ... just to be on the safe side				<v1.2>
    move.l (%a3),%d1 // start of buffer address for write 			<10jul85> BBM
    move.l (%a4),%a0 // get current file write position
    move.l %d7,%d2 // ... get biased position
    add.l ResDataOffset(%a0),%d2 // ... and take away the bias
    bsr WrData // and write out the data left in buffer
    add.l %d0,%d7 // point current write position past last rsrc
    move.l (%a3),%a0 // we don't need this buffer so get rid of it 	<SM47> CSS
    cmpa.l %sp,%a0 // 												<SM47> CSS
    beq.w .L_vCmpFrm_7 // CC=EQ means buffer is on stack 				<SM46> CSS
// <SM46> CSS  This works because (A3) contains the SP at the
// time of creation if we created the buffer on the stack,
// AND the stack is the same depth here and at the time
// of creation.
    move.l %a3,%a0 // Else buffer is in memory						<08jan86> BBM
    _disposhandle // get rid of it 								<08jan86> BBM
    bra.w .L_vCmpFrm_8 // and skip by stack code						<08jan86> BBM

.L_vCmpFrm_7:
    unlk %a3 // Dispose of buffer on stack					<08jan86> BBM

.L_vCmpFrm_8:
    move.l (%a4),%a3 // Derefernce map to write out					<08jan86> BBM

wrDtSize:
 // New lable 									<10oct875> BBM
    move.l %d7,DataSize(%a3) // update current size for the data
WriteMap:

    bclr #MapChanged, MAttr(%a3) // Clear the map-changed bit before write.
    beq.w UpdateResExit // If the map didn't change either, exit.

    move.l DataSize(%a3), %d2 // Get updated data size
    add.l ResDataOffset(%a3), %d2 // Add data offset to get new map offset
    move.l %d2, ResMapOffset(%a3) // Save new map offset from D2
    move.l MapSize(%a3), %d0 // byte count=size of map
    move.l %a3, %d1 // Address of map in memory. D2=map offset.
    bsr WrData // Write the map out!

    moveq #HdrSize, %d0 // byte count=HdrSize (write new header!)		<12jun85> BBM
    bsr WrDataSt // ...and do it!
    bsr SetRFEOF // Set new EOF
UpdateResExit:

.L_UpdateResExit_9:

    movem.l (%sp)+,%a0-%a4/%d0-%d1
    rts

//_______________________________________________________________________________
//
// Routine:		CmpFrmMem (utility)
//
// Arguments:	A1		(i/o.l) 		pointer to the end of compacted region
//				A3		(input.l)		Handle to start of update buffer
//				A4		(input.l)		current map handle
//				D3		(input.l)		pointer to end of update buffer
//				D4		(i/o.l) 		pointer in buffer (end of stuff read in)
//				D5		(i/o.l) 		current file read position - ResDataOffset
//				D6		(input.w)		the current file refnum
//				D7		(i/o.l) 		current file write position - ResDataOffset
//				RLocn(A2) (output.l)	the new file offset for this rsrc
//
// Unchanged:	A2-A7,D1,D3,D6
// Trashed:		A0,D0-D2
//
// Called By:	UpdateResFile
// Calls:		_BlockMove, CmpctRead, _GetHandleSize, NotFound, PartWrite,
//				Ptr1Exit, RLocnNew, WriteSize
//
// Function: 	Compacts resource from Memory (because the ResChanged bit set).
//				Just does compact of one resource.	If cann't read in all of
//				resource into update buffer, writes out compacted stuff and
//				continues until all is done.  Also updates RLocn(A2) as well.
//_______________________________________________________________________________

CmpFrmMem:

    bsr PartWrite // make sure we have some room to play in
    move.l RHndl(%a2),%a0 // get info about resource
    _gethandlesize //
    tst.l %d0 // check to see if purged or zero length 		<02jul85> BBM
    bmi NotFound // if purged exit with CC=MI or CC=EQ			<10jul85> BBM
    bsr.w RLocnNew // update new file position for this rsrc
    bsr.w WriteSize // write size of this resource into buff 		<08jul85> BBM
    move.l %d0,%d2 // check to see if enough room in buff
    add.l %a1,%d2 // ... D2 now points to proposed end
    cmp.l %d2,%d4 // Does it fit inside read buffer?
    move.l (%a0),%a0 // dereference handle to rsrc for calc			<10jul85> BBM
    bge.w Ptr1Exit // CC=GE means it fits so transfer and exit		<10jul85> BBM

    move.l %d0,%d2 // D2 will be the number of bytes left in rsrc
    moveq #0,%d1 // D1 will be offset into resource				<02jul85> BBM
.L_CmpFrmMem_1:
    move.l RHndl(%a2),%a0 // set up A0 to be pointer into resource
    move.l (%a0),%a0 // ... dereference handle
    add.l %d1,%a0 // ... and add in offset 						<02jul85> BBM
    move.l %d3,%d0 // How much room in update buffer?
    sub.l %a1,%d0 // ... D0 has free byte count in buffer
    cmp.l %d2,%d0 // is there room in buffer?
    bge.w .L_CmpFrmMem_7 // CC=GE means enough room						<02jul85> BBM
    add.l %d0,%d1 // update offset into resource					<02jul85> BBM
    sub.l %d0,%d2 // Update number of bytes left
    bsr.w .L_CmpFrmMem_8 // Transfer what we can							<02jul85> BBM
    bsr PartWrite // write out what we can
    bra.w .L_CmpFrmMem_1 // and loop back for the rest of rsrc

.L_CmpFrmMem_7:
    move.l %d2,%d0 // D2 has number of bytes left in rsrc
.L_CmpFrmMem_8:
    move.l %a1,%d4 // update buffer end read pointer				<02jul85> BBM
//		BRA.S	Ptr1Exit				; and exit with block move stuff 				<10jul85> BBM
//	*!*!*!* Heads Up -  Falling Through	; BRA commented out for warning free assembly.	<c396>

//_______________________________________________________________________________
//
// Routine:		Ptr1Exit, Ptr2Exit, Ptr3Exit (utilities)								<10jul85> BBM
//
// Arguments:	A0		(input.l)		pointer to source of blockmove (Ptr1Exit)
//				A1		(i/o.l) 		compacted region pointer
//				D0		(input.l)		size of blockmove or offset to add to D5/A1
//				D4		(output.l)		end of region read into buffer
//				D5		(i/o.l) 		current file read position
//
// Unchanged:	A0,A1-A7,D0-D3,D5-D7
// Trashed:		none
//
// Called By:	CmpFrmMem
// Calls:		none
//
// Function: 	standard exit code for several routines.  Saves a few bytes and
//				makes sure a few tests are done.
//
// Warning:		Entered by falling through from previous function (CmpFrmMem)			<c396>
//_______________________________________________________________________________

Ptr1Exit:

    cmp.l %a0,%a1 // is the block move necessary?					<10jul85> BBM
    beq.w Ptr2Exit // ... no, then skip it
    move.l %d0,-(%sp) // save D0 across Blockmove call
    _blockmove //
    move.l (%sp)+,%d0 // restore the size
Ptr2Exit:

    add.l %d0,%d5 // update file read pointer
    add.l %d0,%a1 // update compated region pointer
Ptr3Exit:

    cmp.l %a1,%d4 // if A1 > D4 then D4 gets A1
    bge.w .L_Ptr3Exit_1 // else just exit
    move.l %a1,%d4 // text is true
.L_Ptr3Exit_1:
    rts //

//_______________________________________________________________________________
//
// Routine:		RLocnNew (utility)
//
// Arguments:	A1		(input.l)		pointer to the end of compacted region
//				A3		(input.l)		Handle to start of update buffer
//				D7		(input.l)		the current file write position
//				RLocn(A2) (i/o.l)		the new file offset for this rsrc
//
// Unchanged:	A0-A7,D0-D7
// Trashed:		none
//
// Called By:	CmpFrmDsk, CmpFrmMem
// Calls:		none
//
// Function: 	Updates resource map entry to the new file offset.
//_______________________________________________________________________________

RLocnNew:

    movem.l %d0-%d1/%a0,-(%sp) // save D0-D1/a0 across call 					<10jul85> BBM
    moveq #-3,%d1 // get ResChanged mask (-3 = $FD = RCBMask)		<30jun85> BBM
    lea RAttr(%a2),%a0 // address of RAttr and RLocn					<10jul85> BBM
    and.b (%a0),%d1 // save old Attr in D1
    move.l %a1,%d0 // calc file position for this rsrc
    sub.l (%a3),%d0 // ... by calc how much stuff still in buff
    add.l %d7,%d0 // ... and adding to current write position
    move.l %d0,(%a0) // write out new location
    move.b %d1,(%a0) // restore Attributes
    movem.l (%sp)+,%d0-%d1/%a0 // restore registers
    rts // and go back to caller

//_______________________________________________________________________________
//
// Routine:		WriteSize, WriteZero (utility)											<08jul85> BBM
//
// Arguments:	A1		(i/o.l) 		pointer to the end of compacted region
//				D0		(input.l)		the size to write into the buffer (WriteZero)
//				D5		(i/o.l) 		the current read pointer
//
// Unchanged:	A0,A2-A7,D0-D4,D6-D7
// Trashed:		none
//
// Called By:	CmpFrmDsk, CmpFrmMem
// Calls:		none
//
// Function: 	Writes D0 into update buffer.  Updates the two pointers A1 & D5.
//				The size is maintained across call in D0.
//_______________________________________________________________________________

WriteZero:

    moveq #0,%d0 // write out a zero resource size into buff
WriteSize:
 //
    move.l %d1,-(%sp) // save D1 across call
    moveq #3,%d1 // write D0 (size) into update buffer
.L_WriteSize_0:
    rol.l #8,%d0 // write size into buffer
    move.b %d0,(%a1)+ // ... still writing
    dbra %d1,.L_WriteSize_0 // ... loop back until done
    addq.l #4,%d5 // update file read pointer
    move.l (%sp)+,%d1 // restore D0-D1
    bra.w Ptr3Exit // and go back to caller

//_______________________________________________________________________________
//
// Routine:		CmpFrmDsk (utility)
//
// Arguments:	A1		(i/o.l) 		pointer to the end of compacted region
//				A2		(input.l)		pointer to resource entry
//				A3		(input.l)		Handle to start of update buffer
//				A4		(input.l)		current map handle
//				D3		(input.l)		end of the update buffer
//				D4		(i/o.l) 		pointer in buffer (end of stuff read in)
//				D5		(i/o.l) 		the current file read position
//				D6		(input.w)		the current file refnum
//				D7		(i/o.l) 		the current file write position
//				RLocn(A2) (output.l)	the new file offset for this rsrc
//
// Unchanged:	A2-A7,D1,D3,D6
// Trashed:		A0,D0,D2
//
// Called By:	UpdateResFile
// Calls:		_BlockMove, CmpctRead, PartRead, PartWrite, Ptr1Exit, Ptr2Exit,
//				RLocnNew, WriteZero
//
// Function: 	Compacts resource from disk (because the ResChanged bit not set).
//				Just does read and compact of one resource.  If cann't read in
//				all of resource into update buffer, writes out compacted stuff
//				and continues until all is done.  Also updates RLocn(A2) as well.
//				Also, D4 is limited as a lower bound by A1.
//_______________________________________________________________________________

CmpFrmDsk:

    move.l RLocn(%a2),%d0 // point to next resource in file
    and.l Lo3Bytes,%d0 // make sure high byte cleared for compare		<08jul85> BBM
    sub.l %d5,%d0 // check to see if this RLocn is resonable
    bge.w .L_CmpFrmDsk_4 // CC=GE offset seems good (past stuff written)
    bsr PartWrite // make sure we have some room to work in
    bsr.w RLocnNew // update rsrc location in map
    bra.w WriteZero // write out zero for size of rsrc & return		<08jul85> BBM
//;;RTS 							; we really return though WriteZero
.L_CmpFrmDsk_4:
    add.l %a1,%d0 // add offset to end of compact region
    addq.l #4,%d0 // add in the size of the rsrc size				<03jul85> BBM
    cmp.l %d0,%d4 // is all of the size in memory???
    bge.w .L_CmpFrmDsk_1 // if it is all in memory go read in size		<10jul85> BBM
    bsr PartWrite // else get some room to work with
    move.l RLocn(%a2),%d0 // get current position of this resource
    and.l Lo3Bytes,%d0 // get rid of the resource attributes
    move.l %d0,%d5 // set up read pointer for next read
    bsr.w PartRead // and read in the size
.L_CmpFrmDsk_1:
    move.l RLocn(%a2),%d0 // point A0 to Size of resouce
    bsr.w RLocnNew // update new position in the map				<02jul85> BBM
    sub.l %d5,%d0 // ... D0 now has delta from D5 to size
    and.l Lo3Bytes,%d0 // ... make sure no garbage in high byte
    move.l %a1,%a0 // ... get pointer into buffer
    add.l %d0,%a0 // ... A0 now points to the size in memory
    bsr OddSize // D0 gets Size of Rsrc; A0 points to Rsrc
    addq.l #4,%d0 // Also include the size of the resource			<13may86> BBM
    subq.l #4,%a0 // and point A0 to the size of the resource		<13may86> BBM
    move.l %d0,%d2 // save size for later
    add.l %a0,%d2 // Check to see if all of rsrc is in buffer
    cmp.l %d2,%d4 // is all of rsrc in buffer?
    bge Ptr1Exit // if all in buffer exit by compacting

    add.l %a0,%d5 // bump D5 pointer by A0-A1 to ‚Ä¶					<C268>
    sub.l %a1,%d5 // ‚Ä¶ keep D5 correct for large files				<C268>
    and.l Lo3Bytes,%d5 // ‚Ä¶ and make sure no garbage in high byte		<C268>

    move.l %d0,%d2 // D2 will be count of stuff left to read in 	<01jan86> BBM
    move.l %d4,%d0 // get the end of the stuff read in
    sub.l %a0,%d0 // calc the amount of rsrc in update buffer
    bsr Ptr1Exit // move it down to compacted space
    move.l %a1,%d4 // since there is no more valid data in buff 	<10jul85> BBM
.L_CmpFrmDsk_2:
    bsr.w PartWrite // write out what we have
    bsr.w PartRead // Read in more of the resource
    beq.w .L_CmpFrmDsk_9 // abort if zero bytes read in					<17oct85> BBM
    sub.l %d0,%d2 // update amount of this resource to read
    move.l %a1,%a0 // calc how much of rsrc we just loaded
    add.l %d2,%a0 // ... A0 now points to end of rsrc
    cmp.l %a0,%d4 // does rest of rsrc lie in buffer?
    bge.w .L_CmpFrmDsk_3 // ... CC=GE means the rest is in mem
    move.l %d4,%d0 // compact and write what we have
    sub.l %a1,%d0 // ... by calc how much we have
    bsr Ptr2Exit // ... updateing pointers and ...
    bra.w .L_CmpFrmDsk_2 // ... looping back

.L_CmpFrmDsk_3:
    move.l %a0,%d0 // calc size of the rest of this rsrc
    sub.l %a1,%d0 // ... D0 now is size
    bra Ptr2Exit // and exit!!!!

.L_CmpFrmDsk_9:
    move.w #mapReadErr,ResErr // mark as error for exit						<21oct85> BBM
    bra Ptr3Exit // and exit back to update						<21oct85> BBM

//_______________________________________________________________________________
//
// Routine:		PartRead (utility)
//
// Arguments:	A1		(input.l)		pointer to the end of compacted region
//				A3		(input.l)		Handle to start of update buffer
//				A4		(input.l)		current map handle
//				D3		(input.l)		end of input buffer
//				D4		(output.l)		pointer in buffer (end of stuff read in)
//				D5		(input.l)		the current file read position
//				D6		(input.w)		the current file refnum
//
// Unchanged:	A0-A7,D0-D3,D5-D7
// Trashed:		none
//
// Called By:	CmpFrmDsk
// Calls:		RdData
//
// Function: 	Reads in as much as it can from the file into the update buffer.
//				Note that if you haven't used all you read in before, this will
//				read in the unused stuff again.  Make sure you compact as much
//				as possible and call PartWrite before you call this routine.
//_______________________________________________________________________________

PartRead:

    movem.l %d0-%d2/%a0,-(%sp) // save these across call
    bsr MaxRFEOF // get EOF in D0 								<12sep85> BBM
    move.l (%a4),%a0 // dereference map handle
    sub.l %d5,%d0 // D0 = amount left to read in.
    sub.l ResDataOffset(%a0),%d0 // take out bias for D5
    and.l Lo3Bytes,%d0 // make sure no garbage in top byte
    move.l %d3,%d1 // get end of buffer 							<12sep85> BBM
    sub.l %a1,%d1 // D1 = amount left in buffer
    cmp.l %d1,%d0 // get minimum of D0/D1
    ble.w .L_PartRead_0 // CC=LE means D0 is already minimum
    move.l %d1,%d0 // else D1 is minimum
.L_PartRead_0:
    move.l %a1,%d1 // set up destination address
    move.l %d5,%d2 // set up file position
    add.l ResDataOffset(%a0),%d2 // take out bias for D5
    move.l %a1,%d4 // set up new end of read region
    add.l %d0,%d4 // update read buffer pointer
//   this should never be needed for 24 bit and 32 bit addressing						<v1.2>
//		AND.L	Lo3Bytes,D4 			; make sure high byte cleared					<v1.2>
    bsr RdData // read data into update buffer (D0 good across call)
    tst.l %d0 // check if zero byte read call issued			<17oct85> BBM
    movem.l (%sp)+,%d0-%d2/%a0 // restore working registers
    rts //

//_______________________________________________________________________________
//
// Routine:		PartWrite (utility)
//
// Arguments:	A1		(i/o.l) 		pointer to end of data compacted in buff
//				A3		(input.l)		Handle to beginning of data buffer
//				A4		(input.l)		Handle to the resource map
//				D4		(i/o.l) 		pointer to end of data read in
//				D6		(input.w)		the current file refnum
//				D7		(i/o.l) 		file write position - ResDataOffset
//
// Unchanged:	A2-A7,D0-D3,D5-D6
// Trashed:		A0
//
// Called By:	CmpFrmDsk
// Calls:		_BlockMove, WrData
//
// Function: 	Writes what has been compacted in the buffer pointed to by (A3),
//				and slides the rest forward updating the correct vars.
//_______________________________________________________________________________

PartWrite:

    movem.l %d0-%d2,-(%sp) // save working registers
    move.l %a1,%d0 // check if anything is ready to be written		<01jul85> BBM
    sub.l (%a3),%d0 // ... get amount already compacted
    and.l Lo3Bytes,%d0 // if zero nothing in buffer
    beq.w .L_PartWrite_9 // ... if nothing then just return

    move.l (%a4),%a0 // Dereference map for D7 bias correction
    move.l %d7,%d2 // ... D2 will be actual file position
    add.l ResDataOffset(%a0),%d2 // ... now D2 is unbiased file write position
    add.l %d2,%d0 // force size to be on a 512 byte boundry
    and.w #0xFE00,%d0 // ... round down to 512 byte boundry
    sub.l %d2,%d0 // restore D0 to be size to write out
    ble.w .L_PartWrite_9 // not enough to write out						<01jul85> BBM

    move.l (%a3),%d1 // Set up Buff address for write
    bsr WrData // do write (NOTE D0 saved across call)

    add.l %d0,%d7 // update file write pointer
    sub.l %d0,%a1 // update end of compact region in buffer
    sub.l %d0,%d4 // update end of valid data read in
    move.l %a1,-(%sp) // save A0/A1 across blockmove call
    move.l (%a3),%a1 // set up destination for blockmove
    move.l (%a3),%a0 // set up source for blockmove
    add.l %d0,%a0 // ... add in offset to source
    add.l %d4,%d0 // set up block count for blockmove
    sub.l %a0,%d0 // ... subtract end pointer
    and.l Lo3Bytes,%d0 // ... make sure A0 didn't have garbage          <01jul85> BBM
    _blockmove //
    move.l (%sp)+,%a1 // restore A1 to be compact pointer
.L_PartWrite_9:
    movem.l (%sp)+,%d0-%d2 // restore working registers
    rts //

//_______________________________________________________________________________
//
// Routine:		PROCEDURE CloseResFile(refNum:	INTEGER);
//
// Arguments:	4(SP)	(input.L)		File refnum
//
// Called By:	A-trap through dispatcher, CloseFiles
// Calls:		CloseFiles, StdMapEntry, _UpdateResFile, GetEntries,
//				_ReleaseResource, GetMap, SetFileIO, _Close,
//				_DisposHandle, StdTwoExit
//
// Function:
//				Updates, compacts, and closes the given resource file.
//				Writes out the possibly changed map and header.  Disposes of all
//				resources referred to in the map.  Unlinks the map from the
//				linked list of resource maps, and disposes of the map handle.
//				If the refnum given is 0, closes ALL resource files.
//_______________________________________________________________________________

CloseResFile:

    move.l MinusOne,LastSPExtra // invalidate font cache for font manager		<C238>
    move.w HSCMapRNum,%d0 // if the HandleScan cache map refnum... 		<06may85> BBM
    cmp.w 4(%sp),%d0 // ... equals this maps refnum then ...
    bne.w .L_CloseResFile_1 // ... blitz the cache else skip next ins.
    clr.l HSCHandle // Invalidate cache								<12mar85> BBM
.L_CloseResFile_1:
    tst.w 4(%sp) // is it the system resource file?
    bne.w ClsRFile // if not, go on, else
    bsr CloseFiles // Close all files other than system

ClsRFile:

    bsr StdMapEntry // Link A6, save regs, get map, exit if none

// Check preventFileFromBeingClosedBit in the resource map.  If
// this bit is set, don‚Äôt allow this file to be closed.

    move.l (%a4),%a1 // Pointer to resource map
    btst #preventFileFromBeingClosedBit,mInMemoryAttr(%a1) // Can this file be closed?
    bne.w .L_ClsRFile_leaveWithoutClosing // No.  Don‚Äôt close it.

.L_ClsRFile_doClose:

    bsr CheckAndFlushFontCache // If we are closing a file w/ fonts we should invalidate the cache
.L_ClsRFile_temp:


// Attempt an update, writing out all resources with the ResChanged bit set,
// updating the map, and compacting the resource file.

    move.w %d6, -(%sp) // Push the refnum
    _updateresfile // Update the file.

// Release all the resources

// This section used to call DisposeResource which checked three things: 		<03mar85> BBM
// handle nil, system reference, and resource marked to be updated.	Since
// UpdateResFile was just called there is no point in checking changed bit
// This loop already checks handle nil and system reference, so why not
// call DisposHandle directly.  In fact, why not save more time by getting
// trap address and avoiding the dispatcher. (comment part of <3mar85> BBM)

    move.l #0x23,%d0 // calling dispose handle a lot so ...			<03mar85> BBM
    _gettrapaddress // ... for speed bypass dispatcher.
    bsr GetRsrcCnt // Get to the entries list.						<12aug85> BBM
    bmi.w .L_ClsRFile_3 // if no entries, don't release resources        <28Jan85> BBM
    move.l %a0,%a4 // (A4) = _DisposeHandle.  Restored in GetMap	<06may85> BBM

    addq #RHndl,%a2 // Bias A2 in the loop (for speed)				<12aug85> BBM
.L_ClsRFile_1:
    move.l (%a2)+,%d0 // Get the handle								<12aug85> BBM
    beq.w .L_ClsRFile_2 // if nil, don't attempt a release, else
    move.l %d0,%a0 // get the handle								<03mar85> BBM
    jsr (%a4) // and release it. (_DisposHandle)				<06may85> BBM
.L_ClsRFile_2:
    addq.w #RESize-4,%a2 // point at the next entry						<12aug85> BBM
    dbra %d4,.L_ClsRFile_1 // if more exist, loop back						<12aug85> BBM

// Unlink the map from the map list. 											<28Jan85> BBM

.L_ClsRFile_3:
    bsr GetMap // Get this map, and previous in D0				<12aug85> BBM
    move.l MNext(%a3), %a0 // Get my next link
    tst.l %d0 // Did I have a previous map? (i.e. <>top?)
    bne.w .L_ClsRFile_4 // If so, leave topMapHndl the same
    move.l %a0, TopMapHndl // Otherwise save my next as top map
    bra.w .L_ClsRFile_5 // ...and handle refnum stuff.

.L_ClsRFile_4:
    move.l %d0, %a1 // Get previous map in AReg
    move.l (%a1), %a1 // Dereference
    move.l %a0, MNext(%a1) // set previous next as my next

.L_ClsRFile_5:
    cmp.l SysMapHndl, %a4 // Am I removing the system map?
    bne.w .L_ClsRFile_6 // If not, handle normally.
    lea TopMapHndl,%a0 // initialize world as in powerup				<22jul85> BBM
    move.l ROMMapHndl,(%a0)+ // make rom map top map							<22jul85> BBM
    move.l ROMMapHndl,(%a0)+ // make rom map system map						<22jul85> BBM
    move.l OneOne,(%a0)+ // set SysMap & curMap to Rom Map RefNum 		<22jul85> BBM
    move.l ROMMapHndl,%a0 // zero next map link							<22jul85> BBM
    move.l (%a0),%a0 // ... dereference rom map handle				<22jul85> BBM
    clr.l MNext(%a0) // ... Zero nextMap handle						<22jul85> BBM
    bra.w .L_ClsRFile_9 // and close.

.L_ClsRFile_6:
    cmp.w CurMap, %d6 // Am I the current map?
    bne.w .L_ClsRFile_9 // No, everything's ok.  If I actually am,
    move.l (%a0), %a0 // Dereference my next (which exists)
    move.w MRefNum(%a0), CurMap // and save its refnum in CurMap.

.L_ClsRFile_9:
    bsr SetFileIO // Set up a file block
    _close // And close it.
    beq.w .L_ClsRFile_7 // if no error use ResErr from UpdateResFile 	<16apr85> BBM
    move.w %d0, ResErr // ... else store this code into ResError		<16apr85> BBM

.L_ClsRFile_7:
    move.l %a4, %a0 // get handle into A0 for dispose
    _disposhandle // Kill map even if error (not recoverable)
    bra.w .L_ClsRFile_8

.L_ClsRFile_leaveWithoutClosing:

    clr.w ResErr
.L_ClsRFile_8:

    bra StdTwoExit // and exit.

//_______________________________________________________________________________
//
// Routine:		FUNCTION CountResources(theType:  ResType): INTEGER;
//
// Arguments:	8(A6)	(input.L)		resource type
//				12(A6)	(output.W)		number of resources with that type
//
// Called By:	A-trap through dispatcher
// Calls:		StdEntry, GetTopMap, TypeScan, NextMap, StdFourExit
//
// Function: 	Given the type, return the number of resources of that type
//				accessable in ALL open maps.
//_______________________________________________________________________________

Count1Resources:

    st ResOneDeep // entry for one map deep call					<22apr85> BBM
CountResources:

    bsr StdEntry // Link A6, save regs

    clr.w 12(%a6) // None of this type found so far
    move.l (%a0),-(%sp) // Pust the type‚Ä¶
    bsr CacheResource // Build the resource cache for that type
    move.l ExpandMem,%a0
    move.l emResourceCache(%a0),%d0
    beq.w .L_CountResources_exitCountResourcesOverride
    move.l %d0,%a0
    move.l (%a0),%a0
    move.l kResourceCount(%a0),%d1 // Get count
    move.w %d1,12(%a6) // Save Result
.L_CountResources_exitCountResourcesOverride:


    bra StdFourExit // else, lets exit.


//_______________________________________________________________________________
//
// Routine:		FUNCTION GetIndResource(theType:ResType; index:INTEGER): Handle;
//
// Arguments:	10(A6)	(input.L)		resource type
//				8(A6),A0(input.W)		index
//				14(A6)	(output.L)		resource handle
//
// Called By:	A-trap through dispatcher
// Calls:		CheckLoad, GetTopMap, NextMap, SaveCache, SkipToEntry,
//				StdEndtry, StdSixExit, TypeScan
//
// Function: 	Given the type and the index, return the resource handle.
//_______________________________________________________________________________

Get1IndResource:

    st ResOneDeep // entry for one map deep call					<22apr85> BBM
GetIndResource:

    bsr StdEntry // Link A6, save regs
    move.w (%a0)+, %d7 // Get index in D7
    move.l (%a0)+, %d3 // and type into D3.
    clr.l (%a0)+ // Assume not found.

    tst.w %d7 // the index<=0 and return not found)			<sm 5> stb<SM8>
    ble get1IndErrExit //												<sm 5> stb

    move.l %d3,-(%sp) // Get the resource type we want
    bsr CacheResource // Build the resource cache
    move.l ExpandMem,%a0
    move.l emResourceCache(%a0),%d0 // Get the cache handle
    beq get1IndErrExit // If no handle, get out
    move.l %d0,%a0
    move.l (%a0),%a0 // Pointer to cache
    moveq #0,%d0
    move.w %d7,%d0 // Get index into cache
    subq #1,%d0 // Cache is zero based
    cmp.l kResourceCount(%a0),%d0 // Check the bounds of the index
    bge.w get1IndErrExit

.L_GetIndResource_validIndex:

    asl.w #3,%d0 // <31> Multiply by 8 to get offset into cache
    lea kCacheHeaderSize(%a0,%d0.w),%a0 // Point to proper entry for this index
    move.l (%a0)+,%a4 // <31> Get the map handle (loaded into A4 for CheckLoad)
    moveq #0,%d4 // <31>
    move.w (%a0),%d4 // <31> Get offset to resource reference list
    move.l (%a4),%a3 // Dereference map handle
    move.w mRefNum(%a3),%d6 // <31> CheckLoad in ROM needs the reference number in D6.
    moveq #0,%d0 // <37>
    move.w mTypes(%a3),%d0 // <37> Get offset to type list
    add.w %d0,%d4 // <37> Calculate offset of resource reference from beginning of type list
    move.l %d0,HSCTOffset // <37> Save offset to type list in the cache
    move.l %d4,HSCROffset // <37> Save offset to resource reference in RMgrHiVars
    move.l %a3,%a2 // Save A3 for CheckLoad
    add.l %d4,%a2 // Point to the resource reference
    move.l %d3,HSCRType // <37> Save the resource type in the HandleScan cache
    move.l %a4,HSCMapHndl // <37> Save the map handle in RMgrHiVars
    move.w %d6,HSCMapRNum // <37> Save map reference number in cache

// Got the correct reference list.  Now see if the resource needs to be loaded.  Call CheckLoad
// to do this.  This requires some setup, since the Resource Manager expects certain
// things from itself.  (Since this routine never calls the Resource Manager to set anything
// up, it needs to be done now.)  A3 contains a pointer to the type list, and another stack
// frame is created in order to make a parameter block in the place RdData expects it. D6
// needs to hold the file reference number of the current map.
//
// <11> The decompressor patch calls _Get1Resource from within CheckLoad to load
//		the decompressor.  At this point, the resource chain is still set up to
//		do override stuff.  To keep the decompressor working, restore the resource
//		chain at this point for _Get1IxResource calls.
//

.L_GetIndResource_notOneDeep:

// WITH ResourceMgrStack

    clr.w ioBlock+ioVRefNum(%a6) // so that it has a IOPB where it
    clr.w ioBlock+ioFileType(%a6) // expects it on the stack
    move.b RMgrPerm,ioBlock+ioPermssn(%a6)
    jsr CheckLoad // Go load the resource if necessary
    move.l %a0,14(%a6) // Return the handle
    move.l %a0,HSCHandle // <37> And save it in the cache
    bra.w get1IndOut

// ENDWITH

get1IndErrExit:

    move.w #ResNotFound,ResErr // ... else mark as an error.
get1IndOut:

    bra StdSixExit // pop 6 bytes and exit




//_______________________________________________________________________________
//
// Routine:		FUNCTION CountTypes:  INTEGER;
//
// Arguments:	8(A6)	(output.W)		number of types
//
// Called By:	A-trap through dispatcher
// Calls:		StdEntry, CountITypes, StdZeroExit
//
// Function: 	Return the number of types in open resource files.
//_______________________________________________________________________________

Count1Types:

    st ResOneDeep // entry for one map deep call					<22apr85> BBM
CountTypes:

    bsr StdEntry // Link A6, save regs
    bsr.w CountITypes // Stack up unique types, D7=count
    unlk %a6 // Pop off the types
    move.w %d7, 8(%a6) // Set function result
    bra StdZeroExit // Unlink A6, restore regs


//_______________________________________________________________________________
//
// Routine:		CountITypes (utility)
//
// Arguments:	D7.W	(output.W)		number of unique types
//
// Called By:	CountTypes, GetIndType, Count1Types, Get1IndType
// Calls:		GetTopMap, GetTypes, NextMap, ChkTypeIgnored
//
// Function: 	Return a list of unique types on the stack, and D7 = the count.
//_______________________________________________________________________________

CountITypes:

    move.l (%sp)+, %a0 // Get return address in A0
    link %a6, #0 // Link A6 for quick unlk of types
    moveq #0, %d7 // No types yet
    bsr GetTopMap // Get the top map

.L_CountITypes_0:
    bsr GetTypes // Get the type list
    bmi.w .L_CountITypes_5 // If no types, go to next map
.L_CountITypes_1:
    move.l TType(%a2), %d3 // Get candidate type in D3
    move.l %sp, %a1 // Set up climber register
    move %d7, %d0 // Get current count
    subq #1, %d0 // Zero based for DBRA
    bmi.w .L_CountITypes_3 // if none yet, just add
.L_CountITypes_2:
    cmp.l (%a1)+, %d3 // is this type already represented?
    dbeq %d0,.L_CountITypes_2 // loop back for count of types					<01may85> BBM
    beq.w .L_CountITypes_4 // If so, go to next type in map

.L_CountITypes_3:
    move.l %d3, -(%sp) // Push the new type on the stack

    addq #1, %d7 // Increment type count
.L_CountITypes_4:
    addq.l #TSize, %a2 // Skip to next type entry
    dbra %d5, .L_CountITypes_1 // and loop back.
.L_CountITypes_5:
    bsr NextMap // Link to next map
    beq.w .L_CountITypes_0 // If there are more, loop back.
FPCallCountIExit:
 // FlashPort hint attachment point <bt>
    jmp (%a0) // and return.  D7=one's based count,
// stack has the types.

//_______________________________________________________________________________
//
// Routine:		PROCEDURE GetIndType(VAR theType: ResType; index: INTEGER);
//
// Arguments:	10(A6)	(input.L)		resource type
//				8(A6),A0(input.W)		resource ID
//				14(A6)	(output.L)		resource handle
//
// Called By:	A-trap through dispatcher
// Calls:		StdEntry, CountITypes, StdSixExit
//
// Function: 	Given the index, return indexed type in the current resource map.
//_______________________________________________________________________________

Get1IndType:

    st ResOneDeep // entry for one map deep call					<22apr85> BBM
GetIndType:

    bsr StdEntry // Link A6, save regs
    move.w (%a0)+, %d2 // Get index number in D2,
    move.l (%a0), %d1 // and VAR address in D1.
    bsr.w CountITypes // Make type list on stack
    move.l %d1, %a0 // Get VAR address in AReg
    clr.l (%a0) // Assume failure
    subq.w #1, %d2 // Make count zero based
    bmi.w .L_GetIndType_9 // if count was <=0, exit with error.
    cmp.w %d2, %d7 // is it > max?
    ble.w .L_GetIndType_9 // if so, use error result.
    asl.w #2, %d2 // (count-1)*4 for offset
    move.l 0(%sp, %d2), (%a0) // Set function result
.L_GetIndType_9:
    unlk %a6 // Pop off the types (linked in CountITypes)
    bra StdSixExit // and exit

//_______________________________________________________________________________
//
// Routine:		FUNCTION UniqueID(theType:	ResType): INTEGER;
//
// Arguments:	8(A6)	(input.L)		resource type
//				12(A6)	(output.W)		Unique ID
//
// Called By:	A-trap through dispatcher
// Calls:		StdEntry, GetTopMap, _Random, TypeScan, SkipToEntry, IDScan,
//				IDScanAll, NextMap, StdFourExit
//
// Function: 	Return a unique ID for a given type.  The ID is unique across
//				ALL maps that are currently open.  The ID is always > 127.				<13> stb
//_______________________________________________________________________________

Unique1ID:

    st ResOneDeep // entry for one map deep call					<22apr85> BBM
UniqueID:

    bsr StdEntry // Link A6, save regs
    move.w Ticks+2, %d7 // Get low ticks for initial ID guess

.L_UniqueID_0:
    bsr GetTopMap // Get the top map
    subq #2, %sp // Make room for result
    _random // Get Bill's random number
    add.w (%sp)+, %d7 // Increment ticks by a random amount.
    and.w #0x7FFF, %d7 // Force it positive for no good reason
    move.l 8(%a6), %d3 // Get type in D3
    move.w %d7, %d2 // Copy ID candidate to D2.

    cmp.w #128, %d2 // is it legal?									<13> stb <SM44> CSS
    blt.w .L_UniqueID_0 // if not, try again								<13> stb

.L_UniqueID_1:
    bsr TypeScan // Try to find the type.
    bmi.w .L_UniqueID_2 // if MI, no such type
    bsr SkipToEntry // Skip to the first entry of that type
    bsr IDScan // Now try to find the ID

    beq.w .L_UniqueID_0 // if found, need a new random ID.
.L_UniqueID_2:
    bsr NextMap // follow link to next map
    beq.w .L_UniqueID_1 // If there is another map, scan it,

    move.w %d2, 12(%a6) // else, Return the unique ID
    bra StdFourExit // and exit.

//_______________________________________________________________________________
//
// Routine:		FUNCTION GetResource(theType: ResType; theID: INTEGER): Handle;
//
// Arguments:	10(A6)	(input.L)		resource type
//				8(A6),A0(input.W)		resource ID
//				14(A6)	(output.L)		resource handle
//
// Called By:	A-trap through dispatcher
// Calls:		StdEntry, GetCurMap, TypeScan, SkipToEntry, IDScan, NextMap,
//				CheckLoad, StdSixExit, GetTopMap, SaveCache
//
// Function: 	Given a type and ID, return the handle to the resource.  Searches
//				all maps starting with the current map and ending with the system
//				map.
// NOTE:
//				May not search all open maps, depending which the current map is.
//_______________________________________________________________________________

Get1Resource:

    .equ returnAddress, 0
    .equ theResID, returnAddress+4
    .equ theResType, theResID+2

    st ResOneDeep // entry for one map deep call		<22apr85> BBM
// IF hasRISCV0ResMgrPatches THEN
    bra realGetRes

GetResource:

// <SM56> BT
// first do GetResource ProcessManager patch if the process manager is around
    move.l ExpandMem,%a0 // get ExpandMem
    tst.w emProcessMgrExists(%a0) // is the process manager here?
    beq.w realGetRes // nope, blow the patch

// the patch checks to see if we have old PrintMgr.o glue trying to print
// by loading the resource and calling it directly.  If so, we need to deal
// with the local/global printer scheme for the glue, since _PROpenDriver isn't
// going to be there to cover for us
    moveq.l #0,%d0
    move.l 6(%sp),%a0 // get res type
    cmp.l #0x50444546,%a0 // is this a potential print entry point?
    bne.w realGetRes // if not, branch
    move.w 4(%sp),%d0 // get res id (whole word)
    cmp.w #7+1,%d0 // is it 0-7?
    bcc.w realGetRes // if not, branch to real call

    movem.l %d1-%d2/%a1,-(%sp) // save work regs
    lea -10(%sp),%sp // room for 2 pointers + return value (on bottom)
    pea 6(%sp) // push ptr to one ptr
    pea 6(%sp) // push ptr to the other ptr
//    _mfgetprtypestrings // get local and global ref nums
    moveq.l #0,%d0 // assume pr types the same
    tst.b (%sp)+ // is pr type locked for this guy?
    beq.w .L_GetResource_sameString // if so, branch (treat as strings equal)

    move.l 0(%sp),%a0 // get first string in a0
    move.l 4(%sp),%a1 // get second string in a1
    moveq.l #0,%d0 // clear out d0
    move.b (%a0)+,%d0 // d0.w <- length of first string
    swap %d0
    move.b (%a1)+,%d0 // d0.w <- length of second string
    _relstring // call it

    move.l 4(%sp),%d2 // save local name

// get here with condition codes saying whether the types are the same
.L_GetResource_sameString:

    addq.w #8,%sp // get rid of the 2 string ptrs
    tst.w %d0 // are they the same?
    beq.w .L_GetResource_GotoPDEF // if equal, branch

// Set the printer type for the current process, give a _Close call to the .Print driver, and
// then give it an _Open call.  If the driver is not in yet, _Close will return an error which
// we ignore.  If it is in, it simple passes it along to the appropriate .XPrint.
    subq.w #2,%sp // room for return result
    move.l %d2,-(%sp) // push local name
    move.w BootDrive,-(%sp) // has to be in sys folder
    move.b #fsCurPerm,-(%sp) // default arg (need to r/w probably)
    _openrfperm // bring to front (already opened)
    move.w (%sp)+,%d2 // save the refnum
    _prdrvropen // Open new driver, thus closing old one
    move.w %d2,-(%sp) // push refnum of (old) local pr type name
    _closeresfile // close old driver's res file
    move.l TopMapHndl,%a0 // get top map
    move.l (%a0),%a0 // hdl -> ptr
    move.l MRefNum(%a0),CurMap // force new guy (who is at top) to be current
.L_GetResource_GotoPDEF:

    movem.l (%sp)+,%d1-%d2/%a1 // restore work regs

// <SM56> BT  end of patch, do the real thing
// ELSE
GetResource_entry2:

// ENDIF

realGetRes:
    bsr StdEntry // Link A6, save regs
    bsr GetCurMap // Get handle to current map
    bpl.w .L_realGetRes_2 // fix an old finder bug with new roms			<25mar85> BBM
    bsr GetTopMap // ... when get resource passed bad RefNum
.L_realGetRes_2:
    move.w (%a0)+, %d2 // Get resource ID in D2
    move.l (%a0)+, %d3 // and type in D3
    clr.l (%a0) // Assume not found

.L_realGetRes_0:
    bsr TypeScan // Try to find the type
    bmi.w .L_realGetRes_1 // if MI, no such type, get next map
    bsr SkipToEntry // Skip to the first entry of that type
    bsr IDScan // Now try to find the ID
    beq.w .L_realGetRes_8 // If EQ, found it! Otherwise try next map
.L_realGetRes_1:
    bsr NextMap // follow link to new map
    beq.w .L_realGetRes_0 // If there is another map, try again,
//<17jun85> BBM	MOVE.W	#ResNotFound,ResErr 	; ...else mark as error 						<16apr85> BBM
    bra.w .L_realGetRes_9 // else not found.

.L_realGetRes_8:
    bsr SaveCache // valid most of cache for handle scan			<30apr85> BBM
    bsr CheckLoad // Check handle, init/load it if needed
    move.l %a0, 14(%a6) // Save result--fall through to exit.
    move.l %a0,HSCHandle // finish validating cache						<30apr85> BBM
.L_realGetRes_9:
    bra StdSixExit // else not found.

//_______________________________________________________________________________
//
// Routine:		FUNCTION GetNamedResource(theType:ResType;theName:Str255):Handle;
//
// Arguments:	12(A6)	(input.L)		resource type
//				8(A6)	(input.L)		pointer to resource name
//				16(A6)	(output.L)		resource handle
//
// Called By:	A-trap through dispatcher
// Calls:		StdEntry, GetCurMap, TypeScan, SkipToEntry, NamedResource,
//				_CmpString, NextEntry, NextMap, CheckLoad, StdExit, SaveCache
//
// Function: 	Given a type and name, return the handle to the resource.
//				Searches all maps starting with the current map and ending with
//				the system map.
// NOTE:
//				May not search all open maps, depending which the current map is.
//_______________________________________________________________________________

Get1NamedResource:

    st ResOneDeep // entry for one map deep call					<22apr85> BBM
GetNamedResource:

    bsr StdEntry // Link A6, save regs
    bsr GetCurMap // Get the current map
    move.l (%a0), %a1 // Get name pointer in A1.
    clr.l 16(%a6) // Assume the resource was not found

.L_GetNamedResource_1:
    move.l 12(%a6), %d3 // Get type of resource in D3 for scan.
    bsr TypeScan // Look for the type
    bmi.w .L_GetNamedResource_4 // If no such type found, get next map
    bsr SkipToEntry // Walk through the entries of this type.

.L_GetNamedResource_2:
    bsr NameResource // Given RNameOff(A2), return name in A0.

    bmi.w .L_GetNamedResource_3 // if CC=MI, didn't find name (A1=target string)
    moveq #0, %d0 // Clear out high bits
    move.b (%a0)+, %d0 // Get length (trash A0!)
    swap %d0 // and put it in high order
    move.b (%a1)+, %d0 // Get length of second string in low order
    _cmpstring // Check equality of strings in A0 and A1
    subq #1, %a1 // Point back to the Pascal string in A1
    beq.w .L_GetNamedResource_8 // If equal, load it in!
.L_GetNamedResource_3:
    bsr NextEntry // Skip to next entry
    beq.w .L_GetNamedResource_2 // If not changed type, get next res
.L_GetNamedResource_4:
    bsr NextMap // Link to next map
    beq.w .L_GetNamedResource_1 // if one exists, go search it,
    move.w #ResNotFound,ResErr // ...else mark as error 						<16apr85> BBM
    bra.w .L_GetNamedResource_9 // ...and exit.

.L_GetNamedResource_8:
    bsr SaveCache // valid most of cache for handle scan			<30apr85> BBM
    bsr CheckLoad // otherwise load it in, if necessary.
    move.l %a0,16(%a6) // Save the handle as function result
    move.l %a0,HSCHandle // finish validating cache						<30apr85> BBM
.L_GetNamedResource_9:
    moveq #8, %d0 // pop off eight bytes of parameter
    bra StdExit // and exit.

//=== RmgrAsm2.a Starts Here =============================================================

//_______________________________________________________________________________
//
// The following routines are all of the same form: GET*.
// Given a parameter, point registers to *.
//_______________________________________________________________________________

//_______________________________________________________________________________
//
// Routine:		GetTopMap (utility)
//
// Arguments::	A4		(input.L)		handle top map
//				A2		(output.L)		pointer top map
//				A3		(output.L)		pointer top map (same as A2)
//				A4		(output.L)		handle top map
//				D0		(output.L)		handle previous map (if BNE GetCurMap)
//				D6		(output.W)		top map refnum
//
// Called By:	CountResources, GetIndResource, CountITypes, UniqueID,
//				GetResource, HandleScan
// Calls:		GetCurMap
//
// Function: 	Set up regs for the current topmost resource map.
//_______________________________________________________________________________

GetTopMap:


// BLANKS ON
// STRING ASIS

    tst.b ResOneDeep // if one is set get current map instead 		<26jun85> BBM
    bne.w GetCurMap // ... else fall through to GetTopMap.
    move.l TopMapHndl, %a4 // Get topMap handle
    move.l (%a4), %a3 // Dereference it
    move.w MRefNum(%a3), %d6 // Get reference number
    move.l %a3, %a2 // Copy it in A2
    rts // Block handle in A4, ptr in A3 and A2

//_______________________________________________________________________________
//
// Routine:		GetCurMap, GetMap (utilities)
//
// Arguments:	D6		(input.W)		map refnum
//				A4		(output.L)		handle to new map
//				A3		(output.L)		pointer to new map
//				A2		(output.L)		pointer to new map
//				D0		(output.L)		handle of previous map
//
// Called By:	(GetCurMap): PreLoad, PreLoad2, RsrcZoneInit, GetResource,
//				GetNamedResource, AddResource
//				(GetMap): SetResFileAttrs, StdMapEntry, CloseResFile
// Calls:		NONE
//
// Function: 	Set up registers for resource map specified by refnum in D6.
//				GetCurMap sets D6 to the current map refnum & calls get map.
//				D0 = 0 if no previous Map.
//_______________________________________________________________________________

GetCurMap:

    move.w CurMap, %d6 // Get current resFile refnum.
GetMap:

    tst.w %d6 // Test refnum (system special, or none)
    bmi NotFound // if -ve, no maps open--exit with error.
    bne.w .L_GetMap_1 // If not zero, use standard resource

    move.w OverrideSysMap(%a6),%d6 // Otherwise use refnum of system map

.L_GetMap_1:
    move.l TopMapHndl, %a4 // Get top map handle
    moveq #0, %d0 // Initialize prev map handle
    bra.w .L_GetMap_3 // Get next map (jump into loop)

.L_GetMap_2:
    move.l MNext(%a3),%d0 // Get handle to the next map.
    beq NotFound // If the handle was nil, not found
    exg %a4,%d0 // set A4, and last map handle in D0.
.L_GetMap_3:
    move.l (%a4),%a3 // Dereference
    cmp.w MRefNum(%a3),%d6 // Is it the right resource map?
    bne.w .L_GetMap_2 // no, skip to the next one

    move.l %a3, %a2 // Copy it in A2
    rts // Block handle in A4, ptr in A3 and A2

//_______________________________________________________________________________
//
// Routine:		NextMap (utility)
//
// Arguments:	A4		(input.L)		resource map handle
//				A3		(output.L)		pointer to next map
//				A4		(output.L)		handle to next map
//				D6		(output.L)		refnum to next map
//
// Called By:	HandleScan, CountIResources, CountITypes, UniqueID, GetResource,
//				GetNamedResource
// Calls:		NONE
//
// Function: 	Given map handle in A4 Set up map handle, pointer, refnum of
//				next map.  Returns EQ if found, MI if not
//_______________________________________________________________________________

NextMap:

    move.l %a0,-(%sp) // save off a0
    tst.b ResOneDeep // if one deep set, don't get next map           <26jun85> BBM
    beq.w .L_NextMap_1 // ... else do normal stuff

    move.l ExpandMem,%a0 // <41>
    tst.b emScanOverrideMaps(%a0) // <41> Is overriding on?
    beq.w .L_NextMap_3 // <41> No.  don't check the 2 deep bit.


// The requested resource wasn‚Äôt found in the map in A4.  Check if the twoDeepBit is set on this
// map, and if it is, search the next map down.

    move.l (%a4),%a3 // Try the next map, but only if this map has twoDeep set.
    btst #twoDeepBit,mInMemoryAttr(%a3) // Is the twoDeepBit set?
    bne.w .L_NextMap_1 // ... yes ignore oneDeep, so don't zero handle

.L_NextMap_3:
    suba.l %a4,%a4 // Zero Handle and error exit.
    bra.w .L_NextMap_2 // ... and return								<19jun85> BBM

.L_NextMap_1:
    move.l (%a4), %a3 // Dereference map handle
    move.l MNext(%a3), %a4 // Get next handle
.L_NextMap_2:
    move.l (%sp)+,%a0 // restore a0
    move.l %a4, %d0 // Was the handle zero?
    beq NotFound // If no more maps
    move.l (%a4), %a3 // Otherwise dereference
    move.w MRefNum(%a3), %d6 // Get new refnum also
    bra Found // Say it was found

//_______________________________________________________________________________
//
// Routine:		GetTypes (utility)
//
// Arguments:	A4		(input.L)		map handle
//				A2		(output.L)		pointer to first type
//				A3		(output.L)		pointer to type block in map
//				D5		(output.W)		Type count
//
// Unchanged:	A0-A1,A4-A7,D0-D4,D6-D7
// Trashed:		NONE
//
// Called By:	AddNewRef, CountITypes, GetEntries, TypeScan
// Calls:		NONE
//
// Function: 	Given map handle in A4, Returns A2 pointing to the first type.
//				A3 points to type block.  D5 is type count.
//_______________________________________________________________________________

GetTypes:

    move.l (%a4), %a3 // Dereference map handle
    adda.w MTypes(%a3), %a3 // Add offset to type list
    move.l %a3, %a2 // Copy top of block
    move.w (%a2)+, %d5 // Get type count, point to first type
    rts

//_______________________________________________________________________________
//
// Routine:		GetRsrcCnt (utility)													<10may85> BBM
//
// Arguments:	A4		(input.L)		map handle
//				A2		(output.L)		pointer to first rsrc entry
//				A3		(output.L)		trashed
//				D3		(output.W)		the first type
//				D4		(output.W)		number of rsrc entries in map (minus one)
//				D5		(output.W)		D5 = $FFFF (always)
//
// Unchanged:	A0-A1,A4-A7,D0-D2,D6-D7
// Trashed:		A3,D3,D5
//
// Called By:	Preload/Preload2, RomMapEntry, ReDoMap
// Calls:		GetEntries
//
// Function: 	Set up D4 for looping through resource entries in map.	If you
//				need to go through every rsrc in the map call this routine, and
//				do the following loop:
//
//				@1		...Code 				; put your code here
//						ADD.W	#RESize,A2		; point to next entry
//						DBRA	D4,@1			; loop back if more entries
//
//_______________________________________________________________________________

GetRsrcCnt:

    bsr.w GetEntries // set up regs for the count
    bmi.w .L_GetRsrcCnt_9 // if no entries just return with CC=MI
    addq #TCount+2,%a3 // point to 1st count (+2 is for TypeCount)
    add.w %d5,%d4 // D4 already has first count (Getentries)
    bra.w .L_GetRsrcCnt_1 // jump into loop check
.L_GetRsrcCnt_0:
    addq #TSize,%a3 // skip to count in the next type
    add.w (%a3),%d4 // Add this count into D4
.L_GetRsrcCnt_1:
    dbra %d5,.L_GetRsrcCnt_0 // if more types loop back
.L_GetRsrcCnt_9:
    rts // else return with CC= error state

//_______________________________________________________________________________
//
// Routine:		GetEntries (utility)
//
// Arguments:	A4		(input.L)		map handle
//				A2		(output.L)		pointer to the first ID
//				A3		(output.L)		points to type block
//				D3		(output.W)		the first type
//				D4		(output.W)		entry count (of the first type)
//				D5		(output.W)		type count
//
// Unchanged:	A0-A1,A4-A7,D0-D2,D6-D7
// Trashed:		none
//
// Called By:	HandleScan, RsrcZoneInit, CloseResFile, RmveName
// Calls:		GetTypes, SkipToEntry
//
// Function: 	set up regs from map handle for entry munging.
//_______________________________________________________________________________

GetEntries:

    bsr.w GetTypes // A2 points to first type, D5 is NTypes
    move.l TType(%a2), %d3 // Set the type
    bsr.w SkipToEntry // Skip to the first entry of that type
    tst.w %d5 // Set CC's
    rts // ...and return.


//_______________________________________________________________________________
//
// The following routines are all of the same form: *Scan.
// Scan for * within the map.  If found, return CC=EQ.  If not found, return
// CC=MI.  A2 points to the correct entry.
//_______________________________________________________________________________

//_______________________________________________________________________________
//
// Routine:		IDScan (utility)
//
// Arguments:	A2		(input.L)		pointer to first entry of given type
//				D2		(input.W)		ID of desired entry
//				D4		(input.W)		Entry count
//				A2		(output.L)		pointer to correct entry (if found)
//
// Unchanged:	A0-A1,A3-A7,D1-D3,D5-D7
// Trashed:		D0,D4
//
// Called By:	GetResource
// Calls:		NotFound, SubA2Found
//
// Function: 	Searches for desired entry.  If found, return CC=EQ, else CC=MI.
//_______________________________________________________________________________

IDScan:

    moveq #RESize,%d0 // Set up D0 with Rsrc Size for loop 			<01may85> BBM
.L_IDScan_1:
    cmp.w RID(%a2), %d2 // Is it the right ID?  (note: RID=0)
    adda.w %d0,%a2 // Skip to next ID, if any
    dbeq %d4,.L_IDScan_1 // Loop back, unless no more or found ID 		<01may85> BBM

    beq.w SubA2Found // if found, exit by adjusting A2
    bra.w NotFound // If made it to here, not found


//_______________________________________________________________________________
//
// Routine:		TypeScan (utility)
//
// Arguments:	A4		(input.L)		map handle
//				D3		(input.L)		resource type
//				A2		(output.L)		pointer to type entry
//				D4		(output.W)		entry count
//				D5		(output.W)		type count remaining
//
// Called By:	CountIResources, UniqueID, GetResource, AddNewRef,
//				GetNamedResource, RmveCmn
// Calls:		GetTypes, ChkTypeIgnored
//
// Function: 	Set up returned registers from map handle.	CC=MI if no types in
//				map or entry type not found.
//_______________________________________________________________________________

TypeScan:

    bsr.w GetTypes // A2 -->first type, D5 is type count
    bmi.w NotFound // No types listed!
    moveq #TSize,%d0 // Delta to next type							<01may85> BBM
.L_TypeScan_1:
    cmp.l TType(%a2), %d3 // Is it the right type? (note TType=0)
    adda.w %d0,%a2 // Skip to next type
    dbeq %d5,.L_TypeScan_1 // Decrement and try again, unless found 		<01may85> BBM

    beq.w SubA2Found // if found, exit by adjusting A2
    bra.w NotFound // If made it to here, not found


//_______________________________________________________________________________
//
// Routine:		SkipToEntry
//
// Arguments:	A2		(input.L)		pointer to type entry
//				A2		(output.L)		ptr to first res of input type (or NIL)
//				D4		(output.W)		entry count
//				D5		(output.W)		type count remaining
//
// Called By:	GetEntries, CountIResources, UniqueID, GetResource,
//				GetNamedResource, AddNewRef
// Calls:		NONE
//
// Function: 	Set up registers from input type entry pointer. Returns pointing
//				to the first resource entry of that type, or nil.
//				(21may85 - now can handle 64k offsets correctly.)
//_______________________________________________________________________________

SkipToEntry:

    move.w TCount(%a2), %d4 // Copy entry count of first type into D4
    move.l %d0,-(%sp) // make sure to preserve D0						<21may85> BBM
    moveq #0,%d0 // clear top half of D4 for 64k offset
    move.w TOffset(%a2), %d0 // Copy offset to first entry
    move.l %d0,%a2 // move to address reg.
    move.l (%sp)+,%d0 // make sure to restore D0						<21may85> BBM
    add.l %a3, %a2 // Adding in block address gives entry address
    rts // ...and return

//_______________________________________________________________________________
//
// Routine:		Found, NotFound, SubA2Found
//
// Arguments:	D0		(output.L)		CC=EQ is found, CC=MI is Not found
//
// Called By:	(found): nearly everyone
//				(SubA2Found): IDScan
//				(NotFound): nearly everyone
// Calls:		NONE
//
// Function: 	these routines set up D0 and the condition codes for proper rts.
//				SubA2Found also adjusts A2 to point at the 'right' entry.
//_______________________________________________________________________________


SubA2Found:

    suba.w %d0,%a2 // adjust A2 and fall into found.				<01may85> BBM
Found:

    moveq #0, %d0 // Set CC=EQ
    rts

NotFound:

    moveq #-1,%d0 // Set CC=MI
    rts

//_______________________________________________________________________________
//
// Routine:		StdZEntry, Std1Entry, StdEntry (utilities)
//
// Arguments:	A0		(output.L)
//
// Called By:	everyone (well, almost)
// Calls:		NONE
//
// Function: 	Links A6, saves ALL regs except A0 and D0.	A0 is returned
//				pointing to the last parameter, for (A0)+ traversals.
//
// WARNING:		Currently this routine preserves D0.  OpenRFPerm depends on D0
//				not changing across the call.  DO NOT MODIFY.
//				Add Resource also depends on this!!
//_______________________________________________________________________________

StdZEntry:

    clr.w RomMapInsert // don't insert ROM Map                          <09jul85> BBM
Std1Entry:

    clr.b ResOneDeep // disable one deep stuff						<26jun85> BBM
StdEntry:

    move.l JRMgrStdEntry,%a0 // get the vector from the OSJumptable			<1.2> BBM
    jmp (%a0) // and go to the routine 						<1.2> BBM
vRMgrStdEntry:
 // Def to build dispatch entry					<1.2> BBM

// WITH ResourceMgrStack, ExpandMemRec
    clr.w ResErr // Clear the error flag
    move.l (%sp)+, %a0 // Get return address in A0
    link %a6, #localSize // Link A6 for IOStkFrame			<sm6>stb

    clr.w ioBlock+IOVRefNum(%a6) // always default to default volume and ... 	<05sep85> BBM<sm6>stb
    clr.w ioBlock+IOFileType(%a6) // File type Zero etc. 						<05sep85> BBM<sm6>stb
    clr.l ioBlock+ioDirID(%a6) // zero out the dir ID
    clr.b ioBlock+ioPermssn(%a6) // zero out the permissions byte

    movem.l %a1-%a4/%d1-%d7, -(%sp) // Save registers
    suba.l %a4,%a4 // since IO calls sub (A4) make sure even		<03jun85> BBM
    move.l %a0, -(%sp) // Push A0 back on as return
    lea 8(%a6), %a0 // Point A0 at last parameter

    movem.l %a0-%a5/%d0-%d5,-(%sp) // Save work registers for override setup
    move.w CurMap,realCurMap(%a6) // Save a copy of real CurMap
    clr.w doOverrides(%a6) // assume no overrides for now‚Ä¶
    clr.w overrideCurMap(%a6) // assume no overrides for now‚Ä¶

// Save default values for SysMap and SysMapHndl

    move.l SysMapHndl,OverrideSysMapHndl(%a6) // save the handle
    move.w SysMap,OverrideSysMap(%a6) // Get reference number for SysMap


// As far as I can tell, CurMap is only negative before InitResources is called.
// If this is the case, we certainly don't want to save and restore curmap
// to a negative one‚Ä¶

    tst.w CurMap // Special system case, bail‚Ä¶
    bmi .L_vRMgrStdEntry_noOverrideMap // no overrides		( changed short branch to long )	<SM48> kc

    move.l ExpandMem,%a0 // Get the expandmem rec ptr
    tst.b emScanOverrideMaps(%a0) // Is overriding on?
    beq.w .L_vRMgrStdEntry_noOverrideMap // No.  Use the old resource mgr stuff.

    move.w #1,doOverrides(%a6) // overrides are on.
    bsr GetCurMap // Get the Current Map in A4(Handle) and A3 (Ptr)
    bmi.w .L_vRMgrStdEntry_getSystemMap // -> failed.  Use the system map.						<MC5>

    move.w emLastMapOverridden(%a0),%d0 // Get the last map that was overriden					<MC5>
    beq.w .L_vRMgrStdEntry_GetOverrideMap // If refnum is 0, then go get the override map.			<SM48> kc
    cmp.w CurMap,%d0 // If the refnum is the same as last time,				<SM48> kc
    beq.w .L_vRMgrStdEntry_UseCachedMap //    skip all the work									<SM48> kc
.L_vRMgrStdEntry_GetOverrideMap:
 //														<SM48> kc
//														<SM48> kc
    subq #4,%sp //
    move.l %a4,-(%sp) // <23> Find override map for current resource map
    _getoverridemap // Get an override map for it
    move.l (%sp)+,%d0 // Get override map (it may be the map itself)
    beq.w .L_vRMgrStdEntry_getSystemMap // Assume no override map if this map wasn‚Äôt found

    cmp.l %a4,%d0 // did we get an override map?
    beq.w .L_vRMgrStdEntry_getSystemMap // no they're the same‚Ä¶

// We‚Äôve got an override map for the current resource map.  Set the override map to be the
// current map for the duration of this Resource Manager call.

    move.l %d0,%a1
    move.l (%a1),%a0
    move.w mRefNum(%a0),%d0 // Get reference number of override map

    move.l ExpandMem,%a0 // Cache override and overridden refnums 					<SM48> kc
    move.w CurMap,emLastMapOverridden(%a0) // Remember the last map we overrode.						<SM48> kc
    move.w %d0,emOverrideMapRefNum(%a0) // Remember the override map refnum.							<SM48> kc
//												<SM48> kc
.L_vRMgrStdEntry_UseCachedMap:
 //												<SM48> kc
    move.w emOverrideMapRefNum(%a0),%d0 // Get override refnum from the cache			<SM48> kc
//												<SM48> kc
    move.w %d0,CurMap // Make the Resource Manager start with an override map
    move.w %d0,overrideCurMap(%a6) // store the override map.

.L_vRMgrStdEntry_getSystemMap:

    move.w SysMap,%d6
    bsr GetMap // Get the System Map in A4(Handle) and A3 (Ptr)
    bmi.w .L_vRMgrStdEntry_noOverrideMap // -> Couldn't find the map.									<MC5>

    subq #4,%sp //
    move.l %a4,-(%sp) // <23> Find override map for current resource map
    _getoverridemap // Get an override map for it
    move.l (%sp)+,%d0 // Get override map (it may be the map itself)
    beq.w .L_vRMgrStdEntry_noOverrideMap // Assume no override map if this map wasn‚Äôt found

    move.l %d0,OverrideSysMapHndl(%a6) // save the handle
    move.l %d0,%a1
    move.l (%a1),%a0
    move.w mRefNum(%a0),OverrideSysMap(%a6) // Get reference number of override map

.L_vRMgrStdEntry_noOverrideMap:

    tst.b RomMapInsert // do I need to insert ROM Map					<26jun85> BBM
    beq.w .L_vRMgrStdEntry_done // if zero don't add
    moveq #0,%d1 // tell SwapRomMap to add
    bsr.w SwapRomMap // else go add ROM map
.L_vRMgrStdEntry_done:

    movem.l (%sp)+,%a0-%a5/%d0-%d5
    rts // and return.
// ENDWITH

//_______________________________________________________________________________
//
// Routine:		SwapRomMap (utility)													<26jun85> BBM
//
// Arguments:	D1		(input.w)		0 means add map; -1 means remove map
//
// Unchanged:	A0-A7,D0-D7
// Trashed:		NONE
//
// Called By:	StdEntry, StdExit
// Calls:		NONE
//
// Function: 	called by standard entry and exit to either add in the rommap to
//				the map chain, or delete the rommap from the map chain.
//
//				Now that we have overrides, we want to put the ROM map at the
//				very end of the resource chain when swapping it out
//				and we want to put it in front of any system override maps
//				when swapping in‚Ä¶
//_______________________________________________________________________________

SwapRomMap:
 //
    movem.l %a0-%a3/%d0-%d2,-(%sp) // Save all regs used
    move.l %d1,-(%sp) // save d1 trashed by GetOverrideMap

    movea.l OverrideSysMapHndl(%a6),%a3 // Hold the override map in a3 the link we're looking for
    move.l (%sp)+,%d1 // restore d1 trashed by GetOverrideMap
    movea.l ROMMapHndl,%a2 // A2 has the ROM Map
    cmpa.l %a3,%a2 // if A3=A2 then only ROM map around so...
    beq .L_SwapRomMap_9 // ... just return

//
//	If we are swapping in‚Ä¶
//		if CurMap == SysMap then make CurMap= ROMMap (1)
//	else if we are swapping out‚Ä¶
//		if CurMap == ROMMap then make CurMap SysMap
//

    tst.l %d1 // determine if we are inserting or removing the rommap
    bne.w .L_SwapRomMap_removing // -1 means removing ROM Map

    move.w CurMap,%d0 // for check if curmap needs changing			<10jul85> BBM

.L_SwapRomMap_0:
    cmp.w OverrideSysMap(%a6),%d0 // if SysMap=CurMap								<10jul85> BBM
    bne.w .L_SwapRomMap_1 //
    move.w #1,CurMap // ... then CurMap gets RomMap

//Inserting‚Ä¶ Put the rom resource map above the System Override file‚Ä¶

.L_SwapRomMap_1:
    movea.l TopMapHndl,%a0 // start at top and find map before A0
.L_SwapRomMap_2:
    move.l (%a0),%a0 // dereference it to get pointer
    move.l MNext(%a0),%d0 // get the next map handle into A0
    beq.w .L_SwapRomMap_4 //;;;;;; error!!!
    cmp.l %a3,%d0 // is this the map we want
    beq.w .L_SwapRomMap_3 // found the right map
    move.l %d0,%a0 // Make A0 a handle to the next map
    bra.w .L_SwapRomMap_2 // and loop back for next check

.L_SwapRomMap_3:
    move.l %a2,MNext(%a0) // put in right link
    bra.w .L_SwapRomMap_5 // skip to exit code
.L_SwapRomMap_4:
    cmp.l TopMapHndl,%a3 // if (topmaphndl <> A1) then ...
    bne.w .L_SwapRomMap_5 // ... don't change topmaphndl
    move.l %a2,TopMapHndl // topmap is now the right link

.L_SwapRomMap_5:
    move.l ROMMapHndl,%a0 // Point ROM to sysmap
    move.l (%a0),%a0 // ... Dereference map handle to pointer
    move.l %a3,MNext(%a0) // ... and point ROM to sysmap (possibly overidden)
    bra.w .L_SwapRomMap_exit

.L_SwapRomMap_removing:


    move.w CurMap,%d0 // for check if curmap needs changing			<10jul85> BBM
    cmp.w #1,%d0 // is RomMap=CurMap
    bne.w .L_SwapRomMap_begin //
    move.w OverrideSysMap(%a6),CurMap // ... then make the system the current map

// Find the map that is above the ROM Map so we can point it around the
// ROM Map
.L_SwapRomMap_begin:

    movea.l TopMapHndl,%a0 // start at top and find map before A0
.L_SwapRomMap_loop:
    move.l (%a0),%a0 // dereference it to get pointer
    move.l MNext(%a0),%d0 // get the next map handle into A0
    beq.w .L_SwapRomMap_6 //;;;;;; error!!!
    cmp.l %a2,%d0 // is this the map we want
    beq.w .L_SwapRomMap_7 // found the right map
    move.l %d0,%a0 // Make A0 a handle to the next map
    bra.w .L_SwapRomMap_loop // and loop back for next check

.L_SwapRomMap_6:
    cmp.l TopMapHndl,%a2 // if (topmaphndl <> Rom Map) then ...
    bne.w .L_SwapRomMap_7 // ... don't change topmaphndl
    move.l %a3,TopMapHndl // topmap is now the system map (or its override)
    bra.w .L_SwapRomMap_exit

.L_SwapRomMap_7:
    move.l (%a2),%a1 // Ptr to ROM Map is now in A1
    move.l MNext(%a1),MNext(%a0) // Cut ROM out of the loop‚Ä¶ MapBeforeROMMap->next = ROMMap->next
.L_SwapRomMap_exit:

    move.b ResLoad,%d0 // Swap ResLoad and TmpResLoad
    move.b TmpResLoad,ResLoad // ... Swap continued
    move.b %d0,TmpResLoad // ... Swap continued
.L_SwapRomMap_9:
    movem.l (%sp)+,%a0-%a3/%d0-%d2 // Save all regs used
    rts //

//_______________________________________________________________________________
//
// Routine:		StdExit,StdZeroExit,StdTwoExit,StdFourExit,StdSixExit(utilities)
//
// Arguments:	D0		(input.L)		# of bytes to pop off stack before exit
//				A6		(input.L)		LINKed register
//
// Called By:	everyone
// Calls:		NONE
//
// Function: 	Unlinks A6, restores ALL regs except A0, D0.  Call ResErrHook if
//				ResErr<>0.	ResErrProc called with error in D0 and return
//				address of caller on stack.  The hook can therefore filter
//				errors and return to caller.
//_______________________________________________________________________________

StdTwelveExit:

    moveq #12,%d0 // standard eight byte exit <SM9> <PN>
    bra.w StdExit // and exit <SM9> <PN>
StdTenExit:
 //
    moveq #10,%d0 // standard six byte exit <SM9> <PN>
    bra.w StdExit // and exit <SM9> <PN>
StdEightExit:
 //
    moveq #8,%d0 // standard six byte exit <SM9> <PN>
    bra.w StdExit // and exit <SM9> <PN>
StdSixExit:
 //
    moveq #6,%d0 // standard six byte exit
    bra.w StdExit // and exit
StdFourExit:
 //
    moveq #4,%d0 // standard four byte exit
    bra.w StdExit // and exit
StdTwoExit:
 //
    moveq #2,%d0 // standard two byte exit
    bra.w StdExit // and exit
StdZeroExit:
 //
    moveq #0,%d0 // standard Zero byte exit
RStdExit:
 // <9> rb
StdExit:

    move.l JRMgrStdExit,%a1 // get the vector from the OSJumptable			<1.2> BBM
    jmp (%a1) // and go to the routine 						<1.2> BBM
vRMgrStdExit:
 // Def to build dispatch entry					<1.2> BBM

    tst.w doOverrides(%a6)
    beq.w .L_vRMgrStdExit_noOverrides

    move.w overrideCurMap(%a6),%d1 // did the resource manager change the CurMap?
    beq.w .L_vRMgrStdExit_noOverrides

    cmp.w CurMap,%d1
    bne.w .L_vRMgrStdExit_noOverrides // yes, so leave it alone
    move.w realCurMap(%a6),CurMap // Restore real CurMap

.L_vRMgrStdExit_noOverrides:

    tst.b RomMapInsert // do I need to insert ROM Map					<26jun85> BBM
    beq.w .L_vRMgrStdExit_1 // if zero don't add
    moveq #-1,%d1 // tell SwapRomMap to Delete ROM map
    bsr SwapRomMap // else go add ROM map
.L_vRMgrStdExit_1:
    clr.b ResOneDeep // always stop one deep calls					<09jul85> BBM
    clr.w RomMapInsert // always disable rom map next time				<09jul85> BBM
    movem.l (%sp)+, %a1-%a4/%d1-%d7 // Restore registers
    unlk %a6 // Unlink A6
    move.l (%sp)+, %a0 // Get return address in A0
    add.l %d0, %sp // Pop off parameter

    move.l %a0, -(%sp) // Push return back
    suba.l %a0,%a0 // Zero A0 for Duvall's Edit?                    <19mar85> BBM
    move.w ResErr, %d0 // Get ResErr in D0
    beq.w StdExitOut // If equal, just return, otherwise
FPCallResErrProc:
 // FlashPort hint attachment point <bt>
    move.l ResErrProc, -(%sp) // push error proc
    bne.w StdExitOut // if exists, return, else,
    addq #4, %sp // pop off error proc address
StdExitOut:

    rts // ...and return

//_______________________________________________________________________________
//
// Routine:		HandleScan (utility)
//				RestoreCache (utility
//
// Arguments:	A1		(input.L)		resource handle
//				A2		(output.L)		ptr to entry with correct handle (or nil)
//				A3/A4/D3-D6 			Normal setup into map where handle found
//
// Called By:	(HandleScan): LoadResource, StdResEntry, RefHandle, AddResource
//				(RestoreCache): GetIndResource
// Calls:		GetTopMap, GetEntries, NextMap, SaveCache
//
// Function: 	Returns A2 pointing to entry with the correct handle, or nil if
//				not found.	Scans all maps.
//_______________________________________________________________________________

HandleScan:

    move.l %a1, %d0 // Check for zero handle
    beq NotFound // Can't have a zero handle, say not found
    cmpa.l HSCHandle,%a1 // is this handle in the cache?					<12mar85> BBM
    beq.w RestoreCache // if yes, go set regs and return
HndlScan:
 // alternate entry for RestoreCache				<18jul85> BBM
    bsr GetTopMap // Point to the topmost map first

    move.l %a0,-(%sp) // save A0 across HandleScan Call				<01may85> BBM
.L_HndlScan_0:
    bsr GetEntries // Point to the resource entries
    bmi.w .L_HndlScan_3 // If no entries, link to next map.
    move.l %a3,%a0 // Keep A0 around as a pointer to types
    addq #TSize,%a0 // point at the next type entry					<20may85> BBM
.L_HndlScan_2:
    addq #2,%a0 // ... point to the next type					<02may85> BBM
.L_HndlScan_1:
    addq #RHndl,%a2 // adjust A2 for speed
    cmpa.l (%a2)+,%a1 // Is this the handle we want?
    dbeq %d4,.L_HndlScan_1 // If more of this type continue 				<01MAY85> BBM/LAK
    beq.w .L_HndlScan_4 // ... yes, exit with save.						<01MAY85> BBM/LAK

    move.l (%a0)+,%d3 // get the type into D3							<20may85> BBM
    move.w (%a0)+,%d4 // get the total of this type into D4			<20may85> BBM
    dbra %d5,.L_HndlScan_2 // if this type exists loop back else			<20may85> BBM

.L_HndlScan_3:
    bsr NextMap // Try to link to next map
    beq.w .L_HndlScan_0 // If there is one, scan it, otherwise
    move.l (%sp)+,%a0 // Restore A0									<01may85> BBM
    bra NotFound // the handle wasn't found.

.L_HndlScan_4:
    suba #RESize,%a2 // Because A2 points to the next Rsrc entry		<01may85> BBM
    bsr.w SaveCache // save this entry in the cache					<12mar85> BBM
    move.l (%sp)+,%a0 // Restore A0									<01may85> BBM
    bra Found // Exit with condition code = EQ

RestoreCache:

    movem.l HSCache,%d3/%a1-%a4 // restore the long regs from cache				<12mar85> BBM
    movem.w HSCEntryCnt,%d4-%d6 // restore the word regs from cache
    add.l (%a4),%a2 // Make A2 a pointer
    add.l (%a4),%a3 // Make A3 a pointer
    cmpa.l RHndl(%a2),%a1 // Is this really the handle in the map? 		<18jul85> BBM
    beq Found // good data means finish restoring cache		<18jul85> BBM
    clr.l HSCHandle // bad data in cache so invalidate it			<18jul85> BBM
    bra.w HndlScan // and go look for the real thing				<18jul85> BBM

//_______________________________________________________________________________
//
// Routine:		SaveCache (utility)
//
// Arguments:	A1		(input.L)		handle to resource
//				A2		(input.L)		pointer to resource entry
//				A3		(input.L)		pointer to resource name
//				A4		(input.L)		map handle
//				D3		(input.L)		resource type
//				D4		(input.W)		current entry count (this map)
//				D5		(input.W)		Total count (-1) of this type
//				D6		(input.W)		Map/file reference number
//				D0		(output.W)		Gets trashed
//
// Called By:	(SaveCache):	GetNamedResource, GetResource, HandleScan
//				(SaveIndCache): GetIndResource
// Calls:		NONE
//
// Function: 	Saves the current resource reg world into HSCache.
//_______________________________________________________________________________

SaveCache:
 //
    move.w MinusOne,GIRIndex // invalidate GetIndResource cache				<27jun85> BBM
SaveIndCache:
 // alternate entry for GetIndResource
    move.l (%a4),%d0 // for speed get make D0 a pointer				<06may85> BBM
    sub.l %d0,%a2 // make A2 an offset as map may move
    sub.l %d0,%a3 // make A3 an offset as map may move
    movem.l %d3/%a1-%a4,HSCache // save long regs into the cache 				<12mar85> BBM
    movem.w %d4-%d6,HSCEntryCnt // save word regs into the cache 				<19mar85> BBM
    add.l %d0,%a2 // restore A2 as a pointer
    add.l %d0,%a3 // restore A3 as a pointer
    rts

//_______________________________________________________________________________
//
// Routine:		NameResource (utility)
//
// Arguments:	A2		(input.L)		pointer to resource entry
//				A0		(output.L)		pointer to resource name
//
// Called By:	CheckLoad, GetNamedResource, GetResInfo
// Calls:		NONE
//
// Function: 	Returns A0 pointing to the resource name (or CC=MI if not found).
//_______________________________________________________________________________

NameResource:

    move.w RNameOff(%a2), %d0 // Get the name offset
    bmi.w .L_NameResource_9 // if negative, return empty string.
    move.l (%a4), %a0 // Dereference map handle
    adda.w MNames(%a0), %a0 // Jump to names block
    add.w %d0, %a0 // and offset to the name
    bra Found // say found.

// if here, named resource failed.

.L_NameResource_9:
    lea NullStr, %a0 // Get address of an empty string,
    bra NotFound // and say not found

//_______________________________________________________________________________
//
// Routine:		NextEntry (utility)
//
// Arguments:	A2		(input.L)		pointer to resource entry
//				D4		(input.W)		Resource entry count
//				D5		(input.W)		Type entry count
//				A2		(output.L)		pointer to next ID resource entry
//				D3		(output.L)		resource type (if changed)
//				D4		(output.W)		number of item types (if changed)
//
// Called By:	CountIResources, GetNamedResource, RmveName
// Calls:		NONE
//
// Function: 	Set up A2 as pointer to the next ID resource entry.  Returns
//				CC=MI if not found, CC=EQ if found, and CC=GT if changed type.
//
//_______________________________________________________________________________

NextEntry:

    add.w #RESize, %a2 // Point to the next entry (sign extended)
    subq.w #1, %d4 // Decrement resource entry count
    bpl Found // Return if still more entries left

    subq.w #1, %d5 // Decrement type entry count
    bmi NotFound // If negative, hit end of map
    move.w TypeCount(%a3), %d0 // Get number of types
    sub.w %d5, %d0 // Get type index
    asl.w #TypeShift, %d0 // *8 gives offset to the type

    move.l TType+2(%a3, %d0), %d3 // Get the type in D3
    move.w TCount+2(%a3, %d0), %d4 // Get number of items in D4
    moveq #1, %d0 // Set CC=GT, for "typeChanged" flag
    rts // and return

//_______________________________________________________________________________
//
// Routine:		DoRomEntry (utility)													<11apr85> BBM
//
// Arguments:	A2		(input.L)		pointer to resource entry
//				A0		(output.L)		handle to resource
//				A1		(output.L)		trashed
//				D0		(output.L)		trashed
//
// Called By:	CheckLoad
// Calls:		_HandleZone, SetPL
//
// Function: 	If the ROM resource has been released, "load it" back in.  This
//				code really just recovers what has always been there.
//_______________________________________________________________________________

DoRomEntry:
 // All new routine								<20aug85> BBM
    move.l %a1,-(%sp) // need to preserve this regs
    move.l RLocn(%a2),%d0 // Get the offset to the ROM rsrc
    and.l Lo3Bytes,%d0 // Get rid of the Attrs
    add.l RomBase,%d0 // D0 now points to the ROM Rsrc
    move.l %d0,%a1 // Save off for later
    move.l RomMapHndl,%a0 // get a pointer to the start of ROZ Zone
    _handlezone //
    move.l -4(%a1),%d0 // Rel handle may have useful hi byte	<C169>
    and.l Lo3Bytes,%d0 // Restrict to 24MB range off header		<C169>
    adda.l %d0,%a0 // Add in offset to MP [-4(A1) is the RelHandle] <C169>
    move.l %a0,RHndl(%a2) // map gets handle to the rsrc

    tst.b ResLoad // test if we want to stuff MP
    beq.w .L_DoRomEntry_9 // CC=EQ means just allocate handle
    move.l %a1,(%a0) // stuff MP with pointer to rsrc in ROM			<29aug85> BBM
    bsr SetPL // set the Purgable/Lock bits in MP
.L_DoRomEntry_9:
    move.l (%sp)+,%a1 // restore regs									<29aug85> BBM
    rts // and exit

********************************************************************************************:


// Rolled in decompression stuff from DecompressorPatch.a 4/14/92 <sm6>stb
//
// To Do:  Rewrite this stuff entirely, because it is showing its organic growth and the
//		  logic is less clear than it could be.

// RECORD DecompressionLocals {a6Link},decr //<17>
    .space (1)*4
    .space (1)*4
    .space 16 // ResourceTemplate.ResTemplateSize
    .space 1 // boolean set if we're doing "old" decompression
    .space 1 // and a little padding
    .space (1)*4 // handy place to keep the current decompressor <sm6>stb
    // .equ localSize, * (current PC)
// ENDR //<17>

********************************************************************************************:

//

//
//    checkload for decompression
//

//
//    description: If we are reading in a packed resource, then unpack the
//
//    data as it is loaded.
//

//
//    new
//
//    operation: If the resource is not compressed,
//
//    or if the handle has data then Jump PlainOldCheckLoad.
//
//    remember ResLoad, Clear ResLoad.
//
//    call PlainOldCheckLoad to allocate the data handle.
//
//    restore ResLoad, if False then exit.
//
//    allocate the data handle (in the right heap), with a
//
//    little extra slop (using resrvemem if locked).
//
//    unpack the data in place.
//
//    trim away the extra slop.
//
//    exit.
//

//
//    input: %a2: 	pointer to resource list entry
//
//    a4: resource map handle
//

//
//    resload: true if load, false if no load
//

//
//    output: %a0: 	handle to resource
//
//    a1: trashed
//
//    a2-a7: unchanged
//
//    d0-d2: trashed
//
//    d3-d7: unchanged
//

//
//    called by:	Resource Manager
//

//
//    register usage:		%d2 - offset of raw data in this resource.
//
//    d3 - handle's zone.
//
//    d4 - resource attributes.
//
//    d5 - unpacked size.
//
//    d6 - offset to this resource's data, then the var table handle.
//
//    d7 - original resLoad state.
//
//    a2/a3/a4 - as the Resource Manager needs them.
//
//    a5 - the resource handle.
//
//    a6 - LINKed locals <17>
//

********************************************************************************************:


CheckLoad:
//  // moved the vectored jump up from the original checkload
    move.l jCheckLoad,%a0 // get the vector from the OSJumptable
    jmp (%a0) // and go to the routine
vCheckLoad:
//  // Def to build dispatch entry

// Force all system resources into the system heap by setting the ResSysHeap bit. <SM37> PN.start <SM54>

    subq #2,%sp
    move.l %a4,-(%sp)
    _isthisasystemresourcemap // Is this the system map or system override?
    tst.b (%sp)+
    bne.w .L_vCheckLoad_system_rsrc // if so, branch
    move.l (%a4),%a0 // a0 <- ptr to map
    btst.b #mapForceSysHeap,MAttr(%a0) // should all entries in map go in system heap?
    beq.w .L_vCheckLoad_GoOnCheckLoad // if not, branch
.L_vCheckLoad_system_rsrc:

    bset #ResSysHeap,RAttr(%a2) // get into the system heap (and stay there!)
    bne.w .L_vCheckLoad_GoOnCheckLoad // branch if was already set
    bset #ResSysRef,RAttr(%a2) // mark that shouldn't be permanent
.L_vCheckLoad_GoOnCheckLoad:
 // <SM37> PN.end

// This snippet checks to see if the resource reference entry already contains a handle
// or not.  If it does, DeanBit is cleared.  If there is no handle (ie, the resource will
// be loaded this time) set DeanBit.  This allows people to determine whether they loaded
// a resource, or if someone else did.
//
    tst.l RHndl(%a2) // See if a master pointer has already been allocated
    beq.w .L_vCheckLoad_setDeanBit
    bclr #kDeanBit,kBrianBits // A master pointer has already been allocated
    bra.w .L_vCheckLoad_goLoadTheResource
.L_vCheckLoad_setDeanBit:

    bset #kDeanBit,kBrianBits // We‚Äôll be loading the resource right now
.L_vCheckLoad_goLoadTheResource:



//is it compressed?	Flag that it isn't in case it gets modified and written.

    btst.b #resExtended,RAttr(%a2)
    bne.w TestResHandle
    bra PlainOldCheckLoad //no, then just do the old checkload stuff.

// if there is already data in the handle, then just call old checkload.

TestResHandle:

    move.l RHndl(%a2),%d0 //is the handle loaded already?
    beq.w DeCompressor //no, then we will try to load it.

    move.l %d0,%a0
    move.l (%a0),%d0 //check if the master pointer is nil
    bne.w PlainOldCheckLoad // it has data, so no need to load, or decompress.

DeCompressor:

// WITH DecompressionLocals, ExtResRecord
    link %a6, #localSize // save space for a read buffer and stuff <17>

    movem.l %d3-%d7/%a4-%a5,-(%sp) //save some regs.

// Remember ResLoad, Set ResLoad False.

    move.b ResLoad,%d7 //remember ResLoad.		?Should I use _GetResLoad and SetResLoad?
    clr.b ResLoad //don't load the data in old checkLoad.

// The resource is compressed but not loaded yet.  Make sure the handle gets created.

    bsr PlainOldCheckLoad //create the handle for the data.
    move.b %d7,ResLoad //restore ResLoad.
    beq.w .L_DeCompressor_IfZExit2

    move.l %a0,%a5 //remember the handle.
    move.l %a0,%d0 //is the handle allocated now?
.L_DeCompressor_IfZExit2:

    beq.w Exit2 // no, then we can't do any more.

    move.b RAttr(%a2),%d4 //remember the attribute flags.

// read in the resource data header.  This gives us decompressed size.

    moveq #16,%d0 //amount we wish to read.
    move.l RLocn(%a2),%d6 //get the relative data location.
    and.l Lo3Bytes,%d6 //mask off the resource attributes.
    move.l %d6,%d1 //this is where we want to start reading from.
    bsr ReadResBuffer //read in the resource data.

// do a robustness check to see if the handle really seems to be compacted.

    cmp.l #RobustnessData,0(%a0) //is it really an extended resource?
    beq.w .L_DeCompressor_TestHeaderVersion

    movem.l (%sp)+,%d3-%d7/%a4-%a5 //restore the regs, but don't pop them yet.
    unlk %a6 // <17>
    bra PlainOldCheckLoad //read in the data, now that resLoad is restored.

.L_DeCompressor_TestHeaderVersion:

    move.w #CantDecompress,%d2 //assume bad extended resource.
    lea DoDonnDecompress(%a6), %a1 //get flag location <17>
    sf (%a1) //set the flag, assuming that's what we want
    cmp.b #CurHeaderVersion,headerVersion(%a0) //is this DonnBits?
    beq .L_DeCompressor_headerVersionOK //yep, okay
    st (%a1) //nope, clear the flag
    cmp.b #greggyHeaderVersion, headerVersion(%a0) //is this GreggyBits?
    bne ErrorExit //no, bail
.L_DeCompressor_headerVersionOK:
 //move along
// Make the pointers into the Map relative offsets, so we can rebuild them after a heap shuffle.

    sub.l (%a4),%a2 // pointer to map entry becomes an offset.
    sub.l (%a4),%a3 // some callers have a block pointer here.

// assume it's just a non-compressed extended resource, compute data size and offset.

    moveq #16,%d2 //offset to start of data for a normal extended attribute resource
    move.l ResSize(%a0),%d5 //get the size, including header.
    sub.l %d2,%d5 //actual amount of data to read.
    btst #resCompressed,extendedAttributes(%a0)
    beq.w HaveSizeAndOffset //not compressed, then we have the size and offset.

// check which def proc to unpack it with.  We use a simple robust caching scheme to avoid _GetResource calls.

    move.w #badExtResource,%d2 //assume an error if it was the wrong version!
    btst #0, DoDonnDecompress(%a6) // are we doing DonnBits <17>
    bne.w .L_DeCompressor_notDonn // no, get ID from revised general header format
    move.w decompressID(%a0),%d5 //get the Donn ID this resource needs
    bra.w .L_DeCompressor_gotID // ...and keep going
.L_DeCompressor_notDonn:

    move.w defprocID(%a0), %d5 // get defProc
.L_DeCompressor_gotID:


// here, we look for decompression defprocs in all files below the current one

    move.w #CantDecompress,%d2 //assume bad extended resource
    move.l %a4,%a1 //get a handle to the first file to look in
.L_DeCompressor_tryMap:

    move.l %a1,%d0 //are we done?
    beq.w ErrorExit //yes, we looked and didn‚Äôt find anything

    move.l (%a1),%a0 //get map pointer
    btst #decompressionPasswordBit,mInMemoryAttr(%a0)
    beq.w .L_DeCompressor_nextFile

    move.w CurMap,-(%sp)
    move.b ResOneDeep,-(%sp) //preserve the depth setting
    move.w ROMMapInsert,-(%sp) //save the value of ROMMapInsert	<15>
    move.l ResErrProc,-(%sp) //remember the ResErrProc		<15>

    move.w mRefNum(%a0),CurMap //get resource from this file
    clr.w ROMMapInsert //don‚Äôt even think about ROM map	<15>
    clr.l ResErrProc //and don‚Äôt call the app.		<15>

    subq #4,%sp
    move.l #DeCompressDefType,-(%sp) //'dcmp'
    move.w %d5,-(%sp)
    _get1resource //get the decompress def. proc.
    move.l (%sp)+,%a0

    move.l (%sp)+,ResErrProc //back to normal					<15>
    move.w (%sp)+,ROMMapInsert //restore ROM guy too			<15>
    move.b (%sp)+,ResOneDeep //restore depth
    move.w (%sp)+,CurMap

    move.l %a0,%d0
    bne .L_DeCompressor_gotDecompressor //if we got the decompressor, then use it

    move.w ResErr,%d0 //if there was no dcmp, go on
    beq.w .L_DeCompressor_nextFile
    cmp.w #resNotFound,%d0 //if there was an error, quit with the error
    bne ErrorExit
.L_DeCompressor_nextFile:

    move.l (%a1),%a1 //get map pointer
    move.l mNext(%a1),%a1 //go on to the next file in the chain
    bra.w .L_DeCompressor_tryMap
.L_DeCompressor_gotDecompressor:


// remember the defproc handle. (mostly 'cause we don't have any registers left...)

    lea DefProcHandle(%a6),%a1 //stuff away the defproc			<sm6>stb
    move.l %a0,(%a1) //stuff away the defproc handle into DefProc

// it is a compressed resource, make sure we can handle it, and get its size from the header.

.L_DeCompressor_HaveDefProc:

    lea ResHeaderBuffer(%a6),%a0 //point into our frame. <17>
    btst #0, DoDonnDecompress(%a6) // are we DonnBits? <17>
    bne.w .L_DeCompressor_cTableDontCare // no, then cTables are not used
    tst.w cTableID(%a0) //is the built in table to be used?

    bne ErrorExit // for now, we don't load decompression tables.
.L_DeCompressor_cTableDontCare:

    moveq #16,%d2 //offset to start reading from.
    move.l actualSize(%a0),%d5 //get the unpacked size.
    moveq #0,%d0 //overRun indicates the amount of extra room we need to
    btst #0, DoDonnDecompress(%a6) // are we DonnBits? <17>
    bne.w .L_DeCompressor_getWordSizedSlop // no, then slop size is a word
    move.b overRun(%a0),%d0 // avoid the input and output pointers crossing during decompression.
    bra.w .L_DeCompressor_addOverRun //
.L_DeCompressor_getWordSizedSlop:
 //
    move.w decompressSlop(%a0), %d0 // get word sized slop size
.L_DeCompressor_addOverRun:
 //
    add.l %d0,%d5 // block size must include overRun.
HaveSizeAndOffset:


// Remember the current heap zone, and switch to the zone of the compressed handle.

    move.l TheZone,%a0 //get the current heap zone.			<SM55>
    move.l %a0,%d3 //remember current heap.

    cmp.l RomMapHndl,%a4 // is it in the ROM map?
    bne.w .L_HaveSizeAndOffset_NotROMMap // then go recover information
//		Break	'RomMapHndl Zone'
    move.l %a4,%a0
    _handlezone // use the zone that the ROM Map comes from.
    bra.w .L_HaveSizeAndOffset_SetRightZone

.L_HaveSizeAndOffset_NotROMMap:

    btst.l #ResSysHeap,%d4 // Is it in the system heap?
    beq.w .L_HaveSizeAndOffset_SetRightZone // No, use same heap
    move.l SysZone,%a0 // otherwise set the system zone for now

.L_HaveSizeAndOffset_SetRightZone:

    move.l %a0,TheZone // and use it, for now.				<SM55>

// If the handle was locked, then we reserve low memory for the bigger unpacked handle.

    btst.l #resLocked,%d4 //should the handle be locked?
    beq.w .L_HaveSizeAndOffset_DoneReserve
    move.l %d5,%d0 //amount of space we will need.
    _resrvmem //reserve room in locked space for the handle.
.L_HaveSizeAndOffset_DoneReserve:


// Reallocate the handle.

    move.l %a5,%a0
    move.l %d5,%d7 //remember new size.
    move.l %d5,%d0
    _reallochandle //set up the unpacked handle's size.
    move.w %d0,%d1
    beq.w .L_HaveSizeAndOffset_GotHandle //did it get allocated without an error?
//		Break	'Error from _ReallocHandle'
    move.w %d1,ResErr
    suba.l %a5, %a5 // return NIL since we don‚Äôt have enough memory					<10>
    bra ExitCLPastHSetState //exit if an error happened. Skip HSetState						<10>
.L_HaveSizeAndOffset_GotHandle:


// compute the amount of data to be read.

    move.l ResHeaderBuffer+ResSize(%a6),%d0 //amount of raw resource data. <17>
    sub.l %d2,%d0 //subtract the (offset of data from the header) from size.

// compute where the data should go.

    move.l (%a5),%a0 //point to the handle base.
    sub.l %d0,%d7 //subtract the data size from the block size.
    add.l %d7,%a0 //point to the last place where the data will fit.

// Read the data into the end part of the handle.

    move.l %d6,%d1 //where the size and resource header start.
    // addq.l #0,%d1  (no-op, ResHeader==ResSize)
    add.l %d2,%d1 // we want to read from beyond the header.
    bsr ReadResData //read the data into the block.
    beq.w .L_HaveSizeAndOffset_NoReadError // do an error check.
    move.w %d0,ResErr //set up res error from I/O error.
//		Break	'error during read'
    bra ExitCheckLoad
.L_HaveSizeAndOffset_NoReadError:


// if it is not a compressed resource, then we are done.

    btst #resCompressed,ResHeaderBuffer+12(%a6) //compressed resource? <17>
    beq ExitCheckLoad

// create the room for the variable table.  It is a percentage of the unpacked size.
    btst #0, DoDonnDecompress(%a6) // DonnBits? <17>
    bne.w dontCallDonn // no, call defProc via new and improved interface
    moveq #0,%d0 //get the ratio of var table to unpacked size.
    move.b ResHeaderBuffer+varTableRatio(%a6),%d0 //<17>
    beq.w .L_HaveSizeAndOffset_HaveVarSize
    addq.l #1,%d0 //round up.
    move.l %d5,%d1 //unpacked size - is it a long?
    swap %d1
    mulu %d0,%d1 //in case it is, scale the high word too.
    swap %d1
    clr.w %d1 //clear out the overflow.
    mulu %d5,%d0
    add.l %d1,%d0
    lsr.l #8,%d0
.L_HaveSizeAndOffset_HaveVarSize:

    addq.l #8,%d0 //add enough room for at least one record.
    move.l %d0,%d1 //save var table size.
    _newhandle //get the table.  Used to be a _NewPtr, but that moves the Realloc'ed handle
    bne ErrorExitBlowoffHandle //blow off the handle and return a nil handle
    _hlock //don‚Äôt let it move
    move.l %a0,%d6 //save var table ptr.

// Finally we are ready to decompress the data in the clone into our larger handle.

    move.l (%a5),%a0
    add.l %d7,%a0 //point to the data.
    move.l %a0,-(%sp) //source ptr
    move.l (%a5),-(%sp) //destination ptr.
    move.l %d6,%a0 //var table handle
    move.l (%a0),-(%sp) //pass var table ptr.
    move.l %d1,-(%sp) //pass var table size
    move.l DefProcHandle(%a6),%a0 //get the defproc handle.						<sm6>stb

    move.l (%a0),%a0
FPCallDonnDecompress:
 // FlashPort hint attachment point <bt>
    jsr (%a0) //call the defproc.	Note: the defProc can't move memory!

// dispose of the var table handle.

    move.l %d6,%a0
    _disposhandle //dispose of the var table handle.
    bra.w cutBackCLHandle

dontCallDonn:

    pea ResHeaderBuffer+ResHeader(%a6) // header ptr <17>
    move.l DefProcHandle(%a6),%a0 // get the defproc handle.						<sm6>stb
    move.l (%a0), %a0 //
    adda.w 0(%a0), %a0 // move to offset of "Prepare" routine
FPCallGreggyPrepare:
 // FlashPort hint attachment point <bt>
    jsr (%a0) // call defProc "Prepare" routine

    move.l (%a5), %a0 // get start of block
    add.l %d7, %a0 // point to the start of the data
    move.l %a0, -(%sp) // source
    move.l (%a5), -(%sp) // destination
    pea ResHeaderBuffer+ResHeader(%a6) // header ptr <17>
    move.l DefProcHandle(%a6),%a0 // get the defproc handle.						<sm6>stb
    move.l (%a0), %a0 //
    adda.w 2(%a0), %a0 // move to offset of "Decompress" routine
FPCallGreggyDecomp:
 // FlashPort hint attachment point <bt>
    jsr (%a0) // call defProc "Decompress" routine

    pea ResHeaderBuffer+ResHeader(%a6) // header ptr <17>
    move.l DefProcHandle(%a6), %a0 //												<sm6>stb
    move.l (%a0), %a0 //
    adda.w 4(%a0), %a0 // move to offset of "Done" routine
FPCallGreggyDone:
 // FlashPort hint attachment point <bt>
    jsr (%a0) // call defProc "Done" routine

cutBackCLHandle:
 // cut back the handle to get rid of the slop.
    move.l ResHeaderBuffer+6(%a6),%d0 //final size. <17>
    move.l %a5,%a0
    _sethandlesize //shrink the handle to it's correct size.

// restore the handle state, and restore the zone.

ExitCheckLoad:


    move.l %a5,%a0 // Black magic code Donn stole from the resource manager.
    lsl.b #3,%d4 // Shift over lock to bit 7 in D4, purge bit to X .
    rol.b #1,%d4 // Rotate Lock to bit 0
    roxr.b #2,%d4 // Rotate L & P to bits 7 & 6
    or.b #0x20,%d4 // Set Resource Flag bit of D4.B
    and.b #0xE0,%d4 // √álear all but LPR bits of D4.B
    _hgetstate // D0 <- LPR bits		Is this needed?
    and.b #0x1F,%d0 // Clear LPR bits only
    or.b %d4,%d0 // Set up new LPR bits
    _hsetstate // MemMgr bits <- D0

ExitCLPastHSetState:
 //														<10>
    move.l %d3,TheZone // restore the zone.					<SM55>

// Make the offsets into the Map pointers again, so they will be valid after a heap shuffle.

    add.l (%a4),%a2 // pointer to map entry.
    add.l (%a4),%a3 // some callers have a block pointer here.
    move.l %a5,%a0 //return the handle.

// MACHINE MC68040 // (should we cache flush on all cpus?)										<18>
// (if we do then make sure to save D1 since trashed by '030 flush routine)	<18>
    cmp.b #cpu68040,CPUFlag // check if we are on a 68040												<18>
    bne.w flushNot040 // If we are then															<18>
FPCallCacheFlush:
 // FlashPort hint attachment point <bt>
    jsr ([jCacheFlush]) // 	flush data cache for consistency!										<18>
// we should really do this cache line by line for speed... maybe later 		<18>
flushNot040:

// MACHINE MC68000

Exit2:

    movem.l (%sp)+,%d3-%d7/%a4-%a5
    unlk %a6 // <17>
    rts

// We got an error cloning the handle, or creating the unpacked handle.	Toss the clone (even if Nil).

ErrorExitBlowoffHandle:

    move.w #MemFullErr,%d2 //not enough to allocate var table
    move.l %a5,%a0
    _disposehandle
ErrorExit:

//*** 	Break	'DeCompressor - not enough RAM - set up the error code for the Resource Man'
    move.w %d2,ResErr //set up the error code for the Resource Man.
    sub.l %a0,%a0 //return with nil.
    bra.w Exit2

// ENDWITH

********************:
//    end of <sm6>stb rollin ************************************

//_______________________________________________________________________________
//
// Routine:		PlainOldCheckLoad (utility)				renamed <sm6>stb
//
// Arguments:	A2		(input.L)		pointer to resource entry
//				A0		(output.L)		handle to resource
//				A1		(output.L)		trashed
//				D0		(output.L)		trashed
//				D1		(output.L)		trashed
//				D2		(output.L)		trashed
//
// Called By:	Preload, Preload2, UpdateResFile, GetIndResource, GetResource,
//				GetNamedResource, LoadResource
// Calls:		RdRsrc, SetPL, _NewHandle, _NewEmptyHandle, SizeRsrc,
//				_ResrvMem, _ReallocHandle
//
// Function: 	Initializes the handle, first-time if needed, and loads in if
//				ResLoad is true.
//
//				In the case of a reference being loaded, branch to CheckRefLoad.
//
//				In the non-reference case, A2 & A3 are made offsets and restored
//				after the call -- therefore they are preserved and correct.
//_______________________________________________________________________________
//
// LoadIt, PrepHandle, GotHandle, LoadRes, ErrLoad, NoResLoad, EndLoad, EndLoad2
// are really local labels, but the code is so twisted it is hard to tell that
// these are all part of CheckLoad.
//_______________________________________________________________________________

PlainOldCheckLoad:

    move.b RAttr(%a2), %d1 // Get resource info from RAttr
    cmp.l RomMapHndl,%a4 // is it in the ROM map? 						<10apr85> BBM
    beq.w DoRomEntry // then go recover information

    move.l theZone, -(%sp) // Save old zone
    btst #ResSysHeap, %d1 // Is it in the system heap?
    beq.w .L_PlainOldCheckLoad_1 // No, use same heap
    move.l SysZone, theZone // otherwise set the system zone for now
.L_PlainOldCheckLoad_1:
    move.l RHndl(%a2), %a0 // Get the handle in A0, if any.
    sub.l (%a4), %a3 // Make A3 an offset
    tst.b ResLoad // should the resource be loaded, if nec.?
    beq.w NoResLoad // no, just create empty handle, if needed.

    move.l %a0, %d0 // Is there a handle yet?
    bne.w GotHandle // If so, go on
    bsr.w PrepHandle // Set up size, reserve mem if necessary.
    _newhandle // create a handle of the right size
    beq.w LoadRes // if successful, load it, else
    bra.w ErrLoad // exit with error.

PrepHandle:

    bsr SizeRsrc // get the size of the resource in D0.
    move.l %d0, %d2 // Save D0=length for routine RdRsrc.			<09mar85> BBM
    sub.l (%a4), %a2 // Make A2 an offset.
    btst #ResLocked, %d1 // Is this going to be a locked handle?
    beq.w .L_PrepHandle_2 // No, allocate as usual, floating in heap.
    _resrvmem // reserve low memory for the locked handle
// (Save/restore A0 across call! .WORD $A040)
.L_PrepHandle_2:
    move.l %d2, %d0 // restore D0 for memory mgr call				<09mar85> BBM
    rts // and return.

GotHandle:

    tst.l (%a0) // is resource already in?
    bne.w EndLoad // if so, just return, otherwise
    bsr.w PrepHandle // Set up size, reserve mem if necessary,
    _reallochandle // and reallocate it to correct size.
    bmi.w ErrLoad // if unsuccessful, don't try to load.

// RELOAD the resource, or possibly LOAD it for the first time.
LoadRes:

    add.l (%a4), %a2 // Restore A2
    move.l %a0, RHndl(%a2) // Set RHndl(A2) for RdRsrc
    bsr RdRsrc // read the resource.
    bsr.w SetPL // set purgable, lock bits in MP
    move.w ResErr, %d0 // was there a read error? (D0 for errLoad)
    beq.w EndLoad // If not, everything is ok.  A2 correct.
    sub.l (%a4), %a2 // Make A2 an offset again for ErrLoad

ErrLoad:

    move.w %d0, ResErr // Report error
    sub.l %a0, %a0 // Zero handle
    bra.w EndLoadA2 // and restore A2.

// ResLoad is FALSE.  If there is already a handle, return,
// ... else allocate new.

NoResLoad:

    move.l %a0, %d0 // Otherwise test the handle
    bne.w EndLoad // If there is one, just return. A2 correct.
    sub.l (%a4), %a2 // Make A2 an offset for NewHandle call.
    _newemptyhandle // Create a handle for load next time			<07mar85> BBM
EndLoadA2:

    add.l (%a4), %a2 // Finally restore A2.
EndLoad:

    move.l %a0, %d0 // Is there a good handle?
    beq.w .L_EndLoad_1 // If not, don't write in map
    move.l %a0, RHndl(%a2) // otherwise copy it in Hndl location

.L_EndLoad_1:
    add.l (%a4), %a3 // Restore A3 for caller
    move.l (%sp)+, theZone // Restore the zone
    rts // ...and return

//_______________________________________________________________________________
//
// Routine:		SetPL (utility)
//
// Arguments:	A0		(input.L)		handle to resource in memory
//				D1		(input.B)		Resource Attribute Byte			<C396>
//				D1		(output.B)		purgeable, lock bits
//
// Called By:	CheckLoad, DoROMEntry									<C169>
// Calls:		NONE
//
// Function: 	Set purgeable, lock, and resource bits in master pointer.  D0 is
//				trashed, high 3 bits of ‚Äúmaster pointer‚Äù are set.		<C169>
//_______________________________________________________________________________

SetPL:

    lsl.b #3, %d1 // Shift over lock to bit 7 in D1,
//   purge bit to X .				<C396>
    rol.b #1, %d1 // Rotate Lock to bit 0				<C396>
    roxr.b #2, %d1 // Rotate L & P to bits 7 & 6
    or.b #0x20, %d1 // Set Resource Flag bit	of D1.B		<C396>
    and.b #0xE0, %d1 // √álear all but LPR bits of D1.B 	<C396>

    _hgetstate // D0 <- LPR bits		<C169>
    and.b #0x1F,%d0 // Clear LPR bits only	<C169>
    or.b %d1,%d0 // Set up new LPR bits	<C169>
    _hsetstate // MemMgr bits <- D0		<C169>
    rts // finally return.

//_______________________________________________________________________________
//
// Routine:		SizeRsrcDecomp (utility)								<sm21>kc
//
// Arguments:	A2		(input.L)		pointer to resource entry
//				D0		(output.L)		size of resource
//
// Called By:	SizeResource											<sm21>kc
// Calls:		RREntry, ReadRsrcLength									<sm21>kc
//
// Function: 	Returns the size of the resource in D0 by reading the size word
//				from the resource file.  If memory manager error, sets ResErr.
//
//				If the resource is marked in the map as compressed,		<sm6>stb
//				then
//					check the extended header.
//					If it has a valid header
//					then
//						read the actual length from the header.
//_______________________________________________________________________________


SizeRsrcDecomp:
 //								<sm21>kc
// start of <sm6>stb rollin from DecompressorPatch.a

// WITH DecompressionLocals, ExtResRecord

    movem.l %d1-%d2/%a0-%a1, -(%sp) // Save regs
    bsr RREntry // Smashes changed, added bits.
    btst.b #resExtended,RAttr(%a2) // is this an extended resource?
    beq.w ReadRsrcLength // nope, treat it normally.		<sm21>kc

.L_SizeRsrcDecomp_CompressedRead:

// make sure this is really a compressed resource by reading in the header data, and testing for our signature.
    link %a6, #localSize // allow for reentrancy
    movem.l %d1/%a0,-(%sp) // save some scratch regs
    moveq #16,%d0 // amount we wish to read.
    move.l RLocn(%a2),%d1 // get the relative data location.
    and.l Lo3Bytes,%d1 // mask off the resource attributes.
    bsr ReadResBuffer // read in the resource data.

// do a robustness check to see if the handle really seems to be compacted.

    cmp.l #RobustnessData,signature(%a0) //is it really an extended resource?
    movem.l (%sp)+,%d1/%a0
    unlk %a6
    bne.w ReadRsrcLength //didn't match, then just do old sizersrc. <sm21>kc

// adjust the position offset to point within the header to the actual length.

    moveq #6,%d0 //get delta to the new position.
    add.l %d0,%d2 //bump the offset by our header and a long. <SM19>

    bra.w ReadRsrcLength //				<sm21>kc
// end of <sm6>stb rollin

// ENDWITH


//_______________________________________________________________________________
//
// Routine:		SizeRsrc (utility)
//
// Arguments:	A2		(input.L)		pointer to resource entry
//				D0		(output.L)		size of resource
//
// Called By:	CheckLoad, RdRsrc, UpdateResFile, SizeResource
// Calls:		RREntry, RdData
//
// Function: 	Returns the size of the resource in D0 by reading the size word
//				from the resource file.  If memory manager error, sets ResErr.
//_______________________________________________________________________________

SizeRsrc:

    movem.l %d1-%d2/%a0-%a1, -(%sp) // Save regs
    bsr.w RREntry // Smashes changed, added bits.
ReadRsrcLength:
 //						<sm21>kc
    subq.l #LenWdLen, %sp // Save space for length longword
    move.l %sp, %d1 // Point buffer to stack
    moveq #LenWdLen, %d0 // Read 4 bytes
    bsr RdData // and read the bytes in! <<RdData2 06may85>>	<30may85> BBM
    move.l (%sp)+, %d0 // Get length in D0
    movem.l (%sp)+, %d1-%d2/%a0-%a1 // Restore regs
    rts // ...and return.

//_______________________________________________________________________________
//
// Routine:		ReadResBuffer, ReadResData (utility)
//
//		Entry:	D0 - number of bytes to read.
//				D1 - relative data location within the resource data block.
//				A0 - pointer to the data buffer [ReadResData only].
//				A4 - resource map handle.
//				A6 - LINKed locals for decompression
//		Exit:	D0 - result code.
//				A0 - pointer to the header data.
//				all other registers saved.
//
// Called By:	SizeRsrc: ReadResBuffer
//				??? : ReadResData
// Calls:		_Read
//
// Function:		Read resource data for D0 bytes into the buffer (A0).
//				ReadResBuffer uses a buffer on the stack in the A6 link.
//
//				Rolled in from DecompressorPatch.a 						<sm6>stb
//_______________________________________________________________________________

// WITH ExtendedResource, ExtResRecord, VarTableRec, ResourceTemplate, DecompressionLocals
ReadResBuffer:

    lea ResHeaderBuffer(%a6),%a0 // <17>
ReadResData:

    movem.l %d1-%d2/%a0-%a3,-(%sp)
    moveq #ioQElSize,%d2 //create a param block on the stack.
    sub.l %d2,%sp

    move.l %a0,ioBuffer(%sp) //place to put the data.
    move.l %sp,%a0 //for the read call.
    move.l (%a4),%a1 //deref the map handle.
    move.w MRefNum(%a1),ioRefNum(%a0) //refNum from the resource map.
    move.l %d0,ioReqCount(%a0) //number of bytes to read.
    move.w #1,ioPosMode(%a0) //read from start of file.
    add.l ResDataOffset(%a1),%d1 //add the base data offset in the file.
    move.l %d1,ioPosOffset(%a0) //computed offset from file start.
    _read //read the resource data.

    add.l %d2,%sp //strip the ioParam block.
    movem.l (%sp)+,%d1-%d2/%a0-%a3
    rts

//_______________________________________________________________________________
//
// Routine:		RdRsrc, RRExit (utility)
//
// Arguments:	A2		(input.L)		pointer to resource entry
//				D2		(input.L)		Size of Resource
//
// Called By:	(RdRsrc): CheckLoad
//				(RRExit): WrRsrc
// Calls:		RREntry, SizeRsrc, RdData
//
// Function: 	Loads the resource into the handle specified in RHndl(A2).	The
//				handle MUST be the correct size.
//_______________________________________________________________________________

RdRsrc:

    movem.l %d0-%d2/%a0-%a1, -(%sp) // Save regs
    move.l %d2,%d0 // get rsrc size into D0 for RdData				<09mar85> BBM
    bsr.w RREntry // Set up the regs for read/write
    addq.l #LenWdLen, %d2 // Skip over the length longword
    move.l (%a1), %d1 // Set buffer address
    bsr RdData // and read the bytes in! <<RdData2 06may85>>	<30may85> BBM <SM25> CSS
RRExit:

    movem.l (%sp)+, %d0-%d2/%a0-%a1 // Restore registers ...
    rts // ...and return!

//_______________________________________________________________________________
//
// Routine:		RREntry (utility)
//
// Arguments:	A2		(input.L)		pointer to resource entry
//				A4		(input.L)		handle to resource map
//				A0		(output.L)		pointer to resource map
//				A1		(output.L)		handle to resource
//				D2		(output.L)		location of entry in file
//
// Called By:	SizeRsrc, RdRsrc, WrRsrc
// Calls:		NONE
//
// Function: 	Given a resource entry pointer, set up registers for read/write
//_______________________________________________________________________________

RREntry:

    andi.b #RCBMask, RAttr(%a2) // Clear resChanged on read/write.
RREntry6:
 // <9> rb
    move.l (%a4), %a0 // Dereference map handle again
    move.l RLocn(%a2), %d2 // Get location long word
    and.l Lo3Bytes,%d2 // Clear high 8 bits.
    add.l ResDataOffset(%a0), %d2 // Add in the data offset
    move.l RHndl(%a2), %a1 // Get the handle in A1
    rts // ...and return.

//_______________________________________________________________________________
//
// Routine:		WrRsrc (utility)
//
// Arguments:	D2
//				A2		(input.L)		pointer to resource entry
//				A4		(input.L)		handle to map
//
// Called By:   UpdateResFile, WrRsrc
// Calls:		RREntry, _GetHandleSize, WrData, RRExit
//
// Function: 	Writes resource specified in RHndl(A2) out to the resource file
//_______________________________________________________________________________

WrRsrc:

    movem.l %d0-%d2/%a0-%a1, -(%sp) // Save regs
    bsr.w RREntry // Set up the regs for read/write
    move.l %a1, %a0
    _gethandlesize // Get the size to write out
    tst.l %d0 // for sizes bigger than 32k 					<15mar85> BBM
    bpl.w .L_WrRsrc_1 // if good size, go on, else
    moveq #0, %d0 // force zero length (purged!)

.L_WrRsrc_1:
    move.l %d0, -(%sp) // Push size on the stack
    move.l %sp, %d1 // Write the size out (buffer addr.=stack)
    moveq #LenWdLen, %d0 // Write 4 bytes
    bsr.w WrData // Write out length word data (was WrData2)		<30may85> BBM
    addq.l #LenWdLen, %d2 // Skip over length longword
    move.l (%a1), %d1 // Read bytes from handle
    move.l (%sp)+, %d0 // Restore the size to D0 for write
    bsr.w WrData // ...and do it! 			 (was WrData2)		<30may85> BBM
    bra.w RRExit // Restore regs and exit.

//_______________________________________________________________________________
//
// Routine:		SetFileIO (utility)
//
// Arguments:	A6		(input.L)		LINK'ed for file I/O
//				D0		(input.L)		number of bytes to read/write
//				D1		(input.L)		IO buffer address
//				D2		(input.L)		IO position offset
//				D6		(input.W)		IORefnum
//				A0		(output.L)		pointer to IO stack frame
//
// Called By:	RdData, WrData, SetRFEOF, CheckGrow, NewMap, CreateResFile,
//				CloseResFile
// Calls:		NONE
//
// Function: 	Points A0 to the I/O Stack Frame, and fills out the Param Block
//				from imput registers.
//_______________________________________________________________________________

SetFileIO:

// WITH ResourceMgrStack //								<sm6>stb
    lea ioBlock+IORefNum(%a6), %a0 // Point to frame at IORefNum	<sm6>stb
    move.w %d6, (%a0)+ // Set IORefnum
    addq #6, %a0 // Skip over unused stuff
    move.l %d1, (%a0)+ // Set IOBuffer
    move.l %d0, (%a0)+ // Set IOByteCount
    addq #4, %a0 // Skip actual byte count
    move.w #1, (%a0)+ // Set IOPosMode=absolute (fsFromStart)
    move.l %d2, (%a0)+ // and finally set IOPosOffset.
    lea ioBlock(%a6), %a0 // Reset A0 for stack frame
    rts // and return.
// ENDWITH //								<sm6>stb

//_______________________________________________________________________________
//
// Routine:		SetFileIODirID (utility)
//
// Arguments:	A6		(input.L)		LINK'ed for file I/O
//				D0		(input.L)		number of bytes to read/write
//				D1		(input.L)		IO buffer address
//				D6		(input.W)		IORefnum
//				A0		(output.L)		pointer to IO stack frame
//
// Called By:	RdData, WrData, SetRFEOF, CheckGrow, NewMap, CreateResFile,
//				CloseResFile
// Calls:		NONE
//
// Function: 	Just like SetFileIO except don't trash ioDirID w/ a posOffset‚Ä¶
//_______________________________________________________________________________
SetFileIODirID:

// WITH ResourceMgrStack //								<sm6>stb
    lea ioBlock+IORefNum(%a6), %a0 // Point to frame at IORefNum	<sm6>stb
    move.w %d6, (%a0)+ // Set IORefnum
    addq #6, %a0 // Skip over unused stuff
    move.l %d1, (%a0)+ // Set IOBuffer
    move.l %d0, (%a0)+ // Set IOByteCount
    addq #4, %a0 // Skip actual byte count
    move.w #1, (%a0)+ // Set IOPosMode=absolute (fsFromStart)
    lea ioBlock(%a6), %a0 // Reset A0 for stack frame
    rts // and return.
// ENDWITH //								<sm6>stb

//_______________________________________________________________________________
//
// Routine:		RdData (utility)
//
// Arguments:	A6		(input.L)		LINK'ed for file I/O
//				D0		(input.L)		number of bytes to read/write
//				D1		(input.L)		IO buffer address
//				D2		(input.L)		IO position offset
//				D6		(input.W)		IORefnum
//				A0		(output.L)		pointer to IO stack frame ??
//
// Called By:	SizeRsrc, RdRsrc, NewMap
// Calls:		SetFileIO, _Read, IOExit
//
// Function: 	Read data according to input registers.  Note A2/A3 made offsets
//				across this call and resored in IOExit
//_______________________________________________________________________________

RdData:

    bsr.w SaveRegs // Saves A1/A0/D1/D0 and makes A2/A3 offsets 	<22aug85> BBM
//	so their handles can move across _Read		<sm6>stb
    bsr.w SetFileIO // Set up a file frame
    _read // Read the bytes
    bra.w IOExit // sets reserr and restores all

//_______________________________________________________________________________
//
// Routine:		WrData, WrDataSt (utilities)
//
// Arguments:	A2,A3	(not used)		May get blitzed!! (see Warning below)
//				A4		(not used)		handle to Map
//				A6		(input.L)		LINK'ed for file I/O
//				D0		(input.L)		number of bytes to read/write
//				D1		(input.L)		IO buffer address
//				D2		(input.L)		IO position offset
//				D6		(input.W)		IORefnum
//				A0		(output.L)		pointer to IO stack frame
//
// Called By:	(WrData): WrRsrc, UpdateResFile, UpdateResFile, CreateResFile
//				(WrDataSt): CreateResFile
//				(IOExit): WrData, RdData, SetRFEOF
//				(RestRegs): SetRFEOF
// Calls:		SetFileIO, _Write, WrData
//
// Function: 	Write Data to file according to input registers.  This write
//				may move map around in the heap, so A2/A3 pointers are always
//				saved across this call (by making them offsets).
//
// NOTE: 		IOExit and RestRegs are the standard returns for RdData, WrData,
//				and SetRFEOF.  Also note that IOExit stuffs result in ResErr.
//_______________________________________________________________________________

WrDataSt:

    moveq #0, %d2 // PosOffset=0
WrData:

    bsr.w SaveRegs // Saves A1/A0/D1/D0 and makes A2/A3 offsets 	<22aug85> BBM
    bsr.w SetFileIO // Set up a file frame
    _write // Write the bytes
IOExit:

    move.w %d0, ResErr // Save file I/O result in ResErr
RestRegs:

    adda.l (%a4),%a3 // restore A3 to a pointer						<30may85> BBM
    adda.l (%a4),%a2 // restore A2 to a pointer						<30may85> BBM
    movem.l (%sp)+, %a0-%a1/%d0-%d1 // Restore regs									<22aug85> BBM
    rts // ...and return

//_______________________________________________________________________________
//
// Routine:		SaveRegs (utility)
//
// Arguments:	A4		(input.L)		handle to a map
//
// Called By:	RdData, WrData, WrDataSt, SetRFEOF, CheckGrow
// Calls:		NONE
//
// Function: 	Standard entry routine that saves A0/A1/D0/D1, and makes A2/A3
//				into offsets.  The reverse operation is RestRegs.  Note that A4
//				must be even else an address error occurs.
//_______________________________________________________________________________

SaveRegs:

    movem.l %a0/%d0/%d1,-(%sp) // Stack now has return address and A0/D1/D0 	<22aug85> BBM
    move.l 12(%sp),%a0 // Get return address into A0 for return.
    move.l %a1,12(%sp) // Stack now has A1/A0/D1/D0.
    suba.l (%a4),%a3 // make A3 an offset
    suba.l (%a4),%a2 // make A2 an offset
    jmp (%a0) // ... and return

//_______________________________________________________________________________
//
// Routine:		SetRFEOF (utility)
//
// Arguments:	A4		(input.L)		handle to a map
//
// Called By:	UpdateResFile, ResizeMap, AllocRes, ReallocRes
// Calls:		ResRW, SetFileIO, _SetEOF
//
// Function: 	Set the resource file EOF, and now (14aug85) checks if map is in
//				front or in back of the data.
//_______________________________________________________________________________

SetRFEOF:

    bsr.w SaveRegs // Saves A1/A0/D1/D0 and makes A2/A3 offsets 	<22aug85> BBM
SetEOF2:

    bsr ResRW // Read/write allowed?
    bne.w RestRegs // if not, just exit!							<22aug85> BBM
    bsr.w SetFileIO // Set up a file block
    bsr.w MaxRFEOF // get the end of the file						<14aug85> BBM
    move.l %d0,IOLEOF(%a0) // Set EOF in parameter block
    _seteof // call the FS
    bra.w IOExit // and exit through common code

MaxRFEOF:

    movem.l %a1/%d1,-(%sp) // save A1/D1 across call						<11sep85> BBM
    move.l (%a4),%a1 // Dereference map handle						<14aug85> BBM
    move.l (%a1)+,%d0 // Get ResDataOffset into D0 					<11sep85> BBM
    move.l (%a1)+,%d1 // Get ResMapOffset into D1
    add.l (%a1)+,%d0 // add in the DataSize to D0
    add.l (%a1)+,%d1 // add in the MapSize to D1
    cmp.l %d1,%d0 // which is greater??
    bpl.w .L_MaxRFEOF_9 // CC=PL means ResDataOffset + DataSize = EOF
    move.l %d1,%d0 // ... else ResMapOffset + MapSize = EOF
.L_MaxRFEOF_9:
    movem.l (%sp)+,%a1/%d1 // restore A1/D1
    rts // and return

//_______________________________________________________________________________
//
// Routine:		GetHS (utility)
//
// Arguments:	A2		(input.L)		pointer to entry
//				D0		(output.L)		size of resource entry + 4
//
// Called By:	ReallocRes, AllocRes
// Calls:		_GetHandleSize
//
// Function: 	Get ResourceSize+4 in D1
//_______________________________________________________________________________

GetHS:

    move.l RHndl(%a2), %a0 // Get the resource handle from A2
    _gethandlesize // Get the size of the handle.
    addq.l #LenWdLen, %d0 // add 4 for length longword 					<06may85> BBM
    move.l %d0, %d1 // Copy to D1 for caller.
    rts

//_______________________________________________________________________________
//
// Routine:		SpaceAt (utility)
//
// Arguments:	A2		(input.L)		pointer to resource entry
//				D0		(output.L)		space available for map
//				CCR 	(output)		BMI for last resource or just before map
//
// Called By:	(SpaceAt): ReallocRes
//				(SpaceAt2): MaxSizeRsrc
// Calls:		NextRes
//
// Function: 	Given A2 pointing to a resource entry, return space available in
//				the map for the resource in D0. Does not include space for a
//				length longword.
//				If last resource, returns MI with D0=space available before map.
//				Otherwise returns PL with D0=space avail before next resource.
//_______________________________________________________________________________

SpaceAt:
 // completely redid routine						<27aug85> BBM
    movem.l %a0-%a2/%d1/%d7,-(%sp) // Save regs
    move.l RLocn(%a2),%d7 // Get the location longword
    and.l Lo3Bytes,%d7 // AND off high 8 bits.
    move.l (%a4),%a0 // dereference map to get at offsets
    move.l ResMapOffset(%a0),%d0 // check to see who is in front
    sub.l ResDataOffset(%a0),%d0 // ... and get delta in D0/A1 with CC's
    move.l %d0,%a1 // ... Save for later
    bmi.w .L_SpaceAt_2 // CC=MI means map is in front of all RSRCs

    cmp.l %d7,%a1 // Is current resource past the map?
    bmi.w .L_SpaceAt_2 // CC=MI means first resource past map

// RSRC is before the map . ..

    bsr ZNextRes // Get following resource--D0 is location.
    bmi.w .L_SpaceAt_1 // If no next resource go get size before map
    cmpa.l %d0,%a1 // If next resource before map ...
    bpl.w satNotLast // ... CC=PL means it is before map

.L_SpaceAt_1:
    move.l %a1,%d0 // Else get biased offset to map in D0
    bra.w satLast // and exit though satLast

// we come here if RSRC is past map . . .

.L_SpaceAt_2:
    bsr ZNextRes // Get following resource--D0 is location.
    bpl.w satNotLast // CC=PL means next resource exists
    bsr.w MaxRFEOF // get EOF in D0 and exit through satLast
    sub.l ResDataOffset(%a0),%d0 // make D0 an offset into the data segment		 <28oct85> BBM

satLast:
 // last RSRC before Map or EOF
    sub.l %d7,%d0 // Get the space available into D0
    moveq #-1,%d7 // Set CC's to MI to tag as 'last' b4 map or LEOF
    bra.w satReturn // and exit though satReturn

satNotLast:

    sub.l %d7,%d0 // Get the space available into D0

satReturn:

    movem.l (%sp)+,%a0-%a2/%d1/%d7 // Restore regs
    rts

//_______________________________________________________________________________
//
// Routine:		ReallocRes, AllocRes (utilities)
//
// Arguments:	A2		(input.L)		pointer to source entry
//				A0		(output.L)		Blitzed
//				D0		(output.L)		Blitzed
//				D1		(output.L)		Blitzed
//
// Called By:	(ReallocRes): ChangedResource
//				(AllocRes): AddResource
// Calls:		GetHS, SpaceAt, SetRFEOF, MaxRFEOF
//
// Function: 	Allocate a space for the resource in the resource file.  If the
//				current location is ok, just return.  If the current location is
//				too small, allocate the resource on the end, updating the
//				ResMapOffset and DataSize.	WARNING!!  THE RESOURCE MUST BE IN
//				MEMORY.  The resfile may be trashed if not.
//
//				AllocRes just bypasses the SpaceAt check, and automatically
//				allocates the resource at the end.	Does not grow the resource
//				file--assumes a previous call to CheckGrow.
//_______________________________________________________________________________

ReallocRes:

    bsr.w GetHS // Get handleSize+4 in D1
    bsr.w SpaceAt // How much space available here?
    cmp.l %d0, %d1 // If space exists at end ...					<27aug85> BBM
    ble SetRFEOF // ... just exit through SetRFEOF
AllocRes:

    bsr.w GetHS // Get handleSize+2 in D1
    bsr.w MaxRFEOF // get end of rsrc file in D0					<27aug85> BBM
    move.l (%a4),%a0 // Dereference map handle						<10oct85> BBM
    sub.l ResDataOffset(%a0),%d0 // get new data size in D0						<10oct85> BBM
    move.b RLocn(%a2),-(%sp) // save off attrs								<10oct85> BBM
    move.l %d0,RLocn(%a2) // get new address								<10oct85> BBM
    move.b (%sp)+,RLocn(%a2) // restore attrs 								<10oct85> BBM
    add.l %d1,%d0 // Get new end of file							<10oct85> BBM
    move.l %d0,DataSize(%a0) // ... and save it								<10oct85> BBM

    ori.b #MCCMask,MAttr(%a0) // Set mapChanged and MapCompact bits
    bsr UpdateOverriddenChangedBits // set the changed bit for any files that may be overridden by the map in A4
    bra SetRFEOF // exit through SetRFEOF.

//_______________________________________________________________________________
//
// Routine:		ResRW (utility)
//
// Arguments:	A4		(input.L)		map handle
//
// Called By:	SetRFEOF, CheckGrow, UpdateResFile, WriteResource
// Calls:		NONE
//
// Function: 	Check the read-only bit (in map attribute byte) and return CC.
//_______________________________________________________________________________

ResRW:

    move.l (%a4), %a0 // Dereference map handle
    btst #MapReadOnly, MAttr(%a0) // Set CC's
    rts // and return.

//_______________________________________________________________________________
//
// Routine:		CheckGrow (utility)
//
// Arguments:	D6		(input.W)		resource file refnum
//				A1		(input.L)		handle to resource to be added to the
//										resource file
//
// Called By:	ChangedResource, SetResInfo, AddResource
// Calls:		_GetHandleSize, _SetHandleSize, ResRW, SetFileIO, _GetEOF,
//				_SetEOF
//
// Function: 	CheckGrow checks for the DiskFull and MemFull conditions.  It
//				assumes the maximum overhead for adding the information to the
//				map, as well as adding the data to the resource file.  256 bytes
//				for max name size, 12 for the resource entry, and 6 for the type
//				entry for a maximum delta of 274 bytes for the map.  If the
//				resource is added to the end of the resource file, the amount
//				that the resource file may need to grow, worst case, will be
//				4+sizeOfResource+274 bytes.  For convenience, 280+sizeOfResource
//				will be used.
//
//				If the resource file in question is read-only, the diskFull test
//				will not be performed.	Be aware of this feature.
//
//				If it is safe to grow both the map handle in memory and the
//				resource file on disk, CheckGrow will return with CC=EQ, and
//				ResErr=0.  If either the memFull or DiskFull conditions occur,
//				CheckGrow will return with CC=MI and ResErr=the error code.
//
//				Keeps A2,A3 correct ptrs (calculates and restores with offsets).
//				Other registers, if pointing inside the map block, may become
//				invalid due to the SetHandleSize moving the block.
//_______________________________________________________________________________

CheckGrow:

    bsr SaveRegs // Saves A1/A0/D1/D0 and makes A2/A3 offsets 	<22aug85> BBM
    move.l %d2,-(%sp) // Save D2										<22aug85> BBM

    moveq #70, %d1 // Get D1=70, then
    asl.w #2, %d1 // *4=280, max overhead possible.
    move.l %a4, %a0 // Get map handle in A0
    _gethandlesize // Get the size
    move.l %d0, %d2 // Save size in D2
    add.l %d1, %d0 // Increase by max overhead
    _sethandlesize // Grow it
    exg %d0, %d2 // Get old size, save error code in D2
    _sethandlesize // and shrink it back.
    move.w %d2,%d0 // Was there an error by first SetHandleSize 	<26aug85> BBM
    bmi.w CG9 // if so, set resErr on exit, D2=code.			<9> rb

    bsr.w ResRW // Read/write allowed?
    bne.w CG9 // if not, exit without doing diskFull check.	<9> rb

    move.l %a1, %d0 // is there a handle to account for?
    beq.w CG1 // if not, don't add length...					<9> rb
    move.l %a1, %a0 // Copy to A0 for GetHandleSize call
    _gethandlesize // get the handle size in D0
    move.l %d0,%d2 // Test long word for error
    bmi.w CG9 // if so, report error							<9> rb
    add.l %d0, %d1 // otherwise add length to other overhead

CheckGrowAt1:
 // 												<9> rb <SM26> rb

CG1:
    bsr SetFileIO // Set up the file frame							<9> rb
    lea IOLEOF(%a0), %a1 // A1 points to EOF loc'n
    _geteof // Get the current EOF
    move.l (%a1), %d2 // Copy it to D2.
    add.l %d1, (%a1) // increment it by maximum grow value
// and set it back.	was it successful?
// Roll in ValidateFileSizeInCheckGrow from ResourceMgrExtensions.a  <SM9><PN>
    tst.b (%a1) // get new size of file set up by CheckGrow (a1 points to ioLEOF inside param block)
// note: 16 meg = $01000000, so if any bits are set in first byte it‚Äôs >= 16 meg
    bne CheckGrowErr // it's ‚â• 16Meg so return an error SM9><PN>
    _seteof // set it back
CG9:
 //												<9> rb
    move.l (%sp)+,%d2 // restore D2									<26aug85> BBM
    bra IOExit // exit by setting CC's and ResErr               <26aug85> BBM
CheckGrowErr:

    move.w #EOFERR,%d0 // not really an eofErr, but it is the end of the resource fork <SM9><PN>
    bra CG9 // SM9><PN>
//_______________________________________________________________________________
//
// Routine:		CheckMap (utility)
//
// Arguments:	A4		(input.L)		map handle
//				D0		(output.L)		Error result
//				CC's    (output.CC)     MI=error;  EQ=good
//
// Called By:	OpenResFile
// Calls:		NONE
//
// Function: 	Check the consistency of the map.  This is some simple error
//				checking for resource files.  It preserves all regs except D0,
//				and returns the error result in D0 and CC's.
// note:
//		test1:	Check to see if the EOF is unreasonable.
//		test2:	Checks to see if offset to Types is odd.  (If so, map is bad.)			<C249>
//		test3:	Checks to see if MNames starts right after all the rsrc entries.
//_______________________________________________________________________________

CheckMap:

    movem.l %a0-%a1/%d0-%d1,-(%sp) // save regs
    move.l (%a4), %a0 // Dereference map handle						<05sep85> BBM
//test1
    bsr MaxRFEOF // get what map thinks to be EOF (trashes A1)	<14aug85> BBM
    cmp.l Lo3Bytes,%d0 // Shouldn't have a file longer than $FFFFFF     <21aug85> BBM
    bge.w .L_CheckMap_8 // ... If EOF is bigger than we're in trouble.
//test2
    btst.b #0,MTypes+1(%a0) // check for an odd offset						<C249>
    bne.w .L_CheckMap_8 // if there is an odd offset, map is bad			<C249>
//test3
    movea.l %a0,%a1 // now both A0 & A1 point to map.
    adda.w MTypes(%a0),%a1 // A1 now points to type stuff
    move.w (%a1)+,%d0 // D0 = # of types minus one (TypeCount) 		<08may85> BBM
    bmi.w .L_CheckMap_1 // if TypeCount is minus no entries
    moveq #0,%d1 // D1 used to sum counts of types
.L_CheckMap_0:
    add.w TCount(%a1),%d1 // add count for this type
    addq #1,%d1 // since TCount is really (n-1)
    addq #TSize,%a1 // point to next type
    dbra %d0,.L_CheckMap_0 // loop back for all types
// note A1 already points to first rsrc entry
    mulu #RESize,%d1 // make offset to end of rsrc entries
    adda.l %d1,%a1 // A1 points just past rsrc entries
.L_CheckMap_1:
    move.w MNames(%a0),%d1 // get name offset into D1						<08may85> BBM
    bgt.w .L_CheckMap_2 // if name offset exists go test it
    suba.l %a0,%a1 // else see if A1 points past end of map
    move.l %a1,%d1 // need to and off high byte
    and.l Lo3Bytes,%d1 // this size should be less than mapsize
    cmp.l MapSize(%a0),%d1 // if CC=GT then ERROR
    bgt.w .L_CheckMap_8 // Goto error
    bra.w .L_CheckMap_7 // else map is good.
.L_CheckMap_2:
    adda.w %d1,%a0 // point A0 where names begin
    cmpa.l %a1,%a0 // if CC=MI then error
    bmi.w .L_CheckMap_8 // BMI to error, fall through to Good
//endtests
.L_CheckMap_7:
    moveq #0,%d0 // No Error
    bra.w .L_CheckMap_9 // ... and skip next instruction
.L_CheckMap_8:
    move.w #mapReadErr,ResErr // Error return
.L_CheckMap_9:
    movem.l (%sp)+,%a0-%a1/%d0-%d1 // restore regs
    rts // and return

//=== RmgrAsm3.a Starts Here =============================================================

//_______________________________________________________________________________
//
// Routine:		PROCEDURE LoadResource(theResource:  Handle);
//
// Arguments:	20(A6)	(input.L)		handle to resource
//
// Called By:	A-trap through dispatcher
// Calls:		StdEntry, HandleScan, CheckLoad, StdZeroExit
//
// Function: 	Given the handle, loads the resource from its resource file,
//				if necessary.  Saves all registers INCLUDING A0 and D0.
//				theResource is 20(A6), over fake return+A0/D0+actual return.
//_______________________________________________________________________________

LoadResource:


// BLANKS ON
// STRING ASIS

    movem.l %a0-%a1/%d0, -(%sp) // Save extra registers
//‚Ä¢‚Ä¢‚Ä¢ begin temp fix
//check if resource is in ROM or not
    move.l 16(%sp),%d0 // get resource handle
    beq.w .L_LoadResource_NilHandle // Is the handle nil     <SM33> PN
    _stripaddress // shave off the variation code (in case its a defproc)	<13> stb
    move.l %d0,%a0 // handle in A0
    move.l ROMBASE,%a1
    cmp.l (%a0),%a1 // resource in ROM
    bpl.w .L_LoadResource_2 // Nope
    clr.w ResErr // since we found a ROM resource, and we assume it is ok. <SM43> CSS
    bra.w .L_LoadResource_9 // yes, done
.L_LoadResource_2:

//‚Ä¢‚Ä¢‚Ä¢ end fix
    pea .L_LoadResource_9 // Fake out stdExit to return here
    bsr Std1Entry // Do standard entry 							<22apr85> BBM


    move.l %d0,%a1 // put stripped handle in a1						<13> stb
    tst.l (%a1) // Was it purged?		<SM33> PN
    bne.w .L_LoadResource_8 // No, don't load		<SM33> PN
    bsr HandleScan // Otherwise set up A2 to point to the entry
    beq.w .L_LoadResource_1 // if found, go on, else

    move.w #ResNotFound,ResErr // Set ResErr
    bra.w .L_LoadResource_8 // and exit.

.L_LoadResource_1:

    move.b ResLoad,-(%sp) // Save current value of ResLoad
    st ResLoad // Set TRUE temporarily, forcing a load.
    bsr CheckLoad // load the resource in, if needed
    move.b (%sp)+,ResLoad // and restore the value of ResLoad
.L_LoadResource_8:

    bra StdZeroExit // Fake out stdExit to not pop params.
.L_LoadResource_NilHandle:

    move.w #ResNotFound,ResErr // set ResError <SM34> PN
.L_LoadResource_9:
 // returns here actually...
    movem.l (%sp)+,%a0-%a1/%d0 // Gotta restore my regs first
    move.l (%sp)+,(%sp) // Pop off handle, preserving retaddr
    rts // ...and return.

//_______________________________________________________________________________
//
// Routine:		ReleaseResource(theResource:  Handle);
//
// Arguments:	8(A6)	(input.L)		handle to resource
//
// Called By:	A-trap through dispatcher
// Calls:		StdResEntry
//
// Function: 	Given handle, releases the resource and disposes of the handle.
//				Any GetResource to the released resource will then cause a disk
//				access and return a new handle.  Error condition handled in
//				StdResEntry.
//				Do not release system override resources from ROM, since there is		<SM32> rb
//				no way to get the back in the resource map.								<SM32> rb
//_______________________________________________________________________________

ReleaseResource:

    bsr StdResEntry // Link, save regs, scan for handle.
    cmpi.w #kFakeResourceOverrideMapRefNum,%d6 // is this a ROM resource ?		<SM32> rb
    beq.w RelExt2 // exit if so									<SM32> rb
    btst #ResChanged, RAttr(%a2) // is it marked to be written?
    bne.w RelExt2 // if so, can't release! So exit.                <12aug85> BBM
    move.l %a1, %a0 // Copy handle to A0
    _disposhandle // Dispose of handle
RelExit:
 // common exit for DetachResource				<12aug85> BBM

    cmp.l #0x464F4E54,%d3 // Check if we should invalidate font cache		<06nov85> BBM
    bne.w .L_RelExit_0 // CC=NE means this is not a font, so skip		<06nov85> BBM
    move.l MinusOne,LastSPExtra // ...else invalidate font cache 				<06nov85> BBM
.L_RelExit_0:
 //
    clr.l RHndl(%a2) // Clear the handle location in map only
    clr.l HSCHandle // Invalidate cache								<12mar85> BBM
RelExt2:
 // common exit for DetachResource				<12aug85> BBM
    bra StdFourExit // and exit.

//_______________________________________________________________________________
//
// Routine:		PROCEDURE DetachResource(theResource:  Handle);
//
// Arguments:	8(A6)	(input.L)		handle to the resource
//
// Called By:	A-trap through dispatcher
// Calls:		StdResEntry
//
// Function: 	Given the handle, releases the resource only.  Does NOT dispose
//				of the handle.	Any GetResource to the released resource will
//				then cause a disk access and return a new handle.  Error
//				condition handled in StdResEntry.
//				Do not detach system override resources from ROM, since there is		<SM32> rb
//				no way to get the back in the resource map.								<SM32> rb
//_______________________________________________________________________________

DetachResource:

    bsr StdResEntry // Link, save regs, scan for handle.				<SM32> rb
    cmpi.w #kFakeResourceOverrideMapRefNum,%d6 // is this a ROM resource ?		<SM32> rb
    beq.w RelExt2 // exit if so									<SM32> rb
    btst #ResChanged, RAttr(%a2) // is it marked to be written?
    bne.w .L_DetachResource_5 // if so, can't detach!                          <03apr85> BBM

    movea.l %a1,%a0 // Gentleman‚Äôs way to say ‚Äúnot an rsrc‚Äù			<C169>
    _hclrrbit //												<C169>
    bra.w RelExit // Common exit code								<12aug85> BBM

.L_DetachResource_5:
    move.w #resAttrErr,ResErr // mark it as an error							<03apr85> BBM
    bra.w RelExt2 // and exit										<12aug85> BBM

//_______________________________________________________________________________
//
// Routine:		PROCEDURE ChangedResource(theResource:	Handle);
//
// Arguments:	8(A6)	(input.L)		handle to the resource
//
// Called By:	A-trap through dispatcher
// Calls:		StdResEntry, CheckGrow, ReallocRes
//
// Function: 	Mark the resource to be written out into the resource file.
//				If protected, won't allow change to occur.  If read-only flag
//				set, won't do the diskFull check, though if at a later time the
//				read-only flag is cleared, it will be written.	Be careful!
//				DiskFull may occur at that time, which will destroy your
//				resource file!	 ResNotFound condition handled in StdResEntry.
//_______________________________________________________________________________

ChangedResource:


    bsr.w StdResEntry // Link, save regs, scan for handle.
    bsr FlushChangedFONDs // flush 'FOND' candidate list if this is a FOND	<sm 5> stb
    movea.l %a1,%a0 // Save state of lock/purge/res bits in D7		<C169>
    _hgetstate // D0 <- LPR bits								<C169>
    move.b %d0,%d7 //												<C169>
    _hnopurge // don‚Äôt allow purge here (CheckGrow)			<C169>
    btst #ResProtected, RAttr(%a2) // Is it protected? (also checks if ROM)
    bne.w .L_ChangedResource_5 // if so, don't allow change.                    <16apr85> BBM
    bsr CheckGrow // Can it grow resource file + map?
    bmi.w .L_ChangedResource_9 // if not, just exit with error, otherwise
    bset #ResChanged, RAttr(%a2) // Set the changed bit for this resource.
    bsr ReallocRes // and allocate it in-place or at end.
    move.l (%a4),%a0 // dreference map handle 						<28jan85> BBM
    bset #mapChanged,MAttr(%a0) // ***Hack! really want MapUpdate				<29jan85> BBM
    bsr UpdateOverriddenChangedBits // set the changed bit for any files that may be overridden by the map in A4
    bra.w .L_ChangedResource_9 // and exit										<16apr85> BBM

.L_ChangedResource_5:
    move.w #resAttrErr,ResErr // mark it as an error return					<16apr85> BBM
.L_ChangedResource_9:

    movea.l %a1,%a0 // A0 <- handle									<C169>
    move.b %d7,%d0 // D0 <- saved LPR bits							<C169>
    _hsetstate //												<C169>
    bra StdFourExit // exit

//_______________________________________________________________________________
//
// Routine:		PROCEDURE WriteResource(theResource:  Handle);
//
// Arguments:	8(A6)	(input.L)		handle to the resource
//
// Called By:	A-trap through dispatcher
// Calls:		StdResEntry, ResRW, WrRsrc
//
// Function: 	Given the handle, writes out the resource back to the resource
//				file.  ResChanged is checked, and the resource not written
//				unless TRUE.  Error condition handled in StdResEntry.
//_______________________________________________________________________________

WriteResource:

    bsr.w StdResEntry // Link, save regs, scan for handle.
    bsr FlushResourceCache
    bsr ResRW // Read/write allowed on this file?
    bne.w .L_WriteResource_9 // if not, don't allow the write!

    btst #ResChanged, RAttr(%a2) // test resChanged attribute
    beq.w .L_WriteResource_9 // Don't write if not changed.
    bsr WrRsrc // Else write out the changed resource
// and exit without error						<12aug85> BBM
.L_WriteResource_9:
    bra StdFourExit // pop 4 bytes and exit.

//_______________________________________________________________________________
//
// Routine:		StdResEntry (utility)
//
// Arguments:	8(A6)	(input.L)		handle to the resource
//				xx		(output.x)		output from HandleScan
//
// Called By:	ReleaseResource, DetachResource, WriteResource, HomeResFile,
//				ChangedResource, SizeResource, MaxSizeRsrc, RsrcMapEntry
// Calls:		Std1Entry, HandleScan, StdFourExit
//
// Function: 	Given the handle 8(A6), scans for the handle and exits if not
//				found.	StdEntry returns with A0 pointing to the last argument
//				(the handle).  Callers should exit through StdFourExit.
//_______________________________________________________________________________

Std2ResEntry:

    move.l MinusOne,12(%sp) // Assume it can't be found                      <21aug85> BBM
StdResEntry:

    move.l (%sp)+, %d0 // Get return address
    bsr Std1Entry // Link A6, save regs							<22apr85> BBM
    move.l %d0, -(%sp) // Push return back.
    move.l %a0,%d0 // Check for nil handle							<22may85> BBM
    beq.w .L_StdResEntry_5 // if nil return with error						<22may85> BBM
    move.l (%a0), %a1 // Get the resource handle
    bsr RefHandle // Scan for it									<26aug85> BBM
    beq.w .L_StdResEntry_9 // If found, just return
.L_StdResEntry_5:
    addq #4, %sp // Otherwise, pop the return address
    bra StdFourExit // and exit.
.L_StdResEntry_9:
    rts // ... else return

//_______________________________________________________________________________
//
// Routine:		FUNCTION HomeResFile(theResource: Handle): INTEGER;
//
// Arguments:	8(SP)	(input.L)		handle to the resource
//				12(A6)	(output.W)		refnum of resource file that has handle
//
// Called By:	A-trap through dispatcher
// Calls:		StdResEntry, StdFourExit
//
// Function: 	Given a handle, returns the refnum of the resource file that the
//				resource lives in.	If the resource is not found, returns -1.
//				Error condition handled in StdResEntry.
//_______________________________________________________________________________

HomeResFile:

    move.w #-1,8(%sp) // Assume failure.								<06may85> BBM
    bsr.w StdResEntry // Get the resource, and return if NIL.
    cmp.w SysMap, %d6 // Is it the system or rom map?					<16may85> BBM
    bne.w .L_HomeResFile_1 // No, leave refnum,
.L_HomeResFile_0:
    moveq #0, %d6 // else return 0 for homeResFile value.

.L_HomeResFile_1:
    move.w %d6, 12(%a6) // Return map refnum as function result
    bra StdFourExit // and exit.

//_______________________________________________________________________________
//
// Routine:		PROCEDURE SetResPurge(install:	BOOLEAN);
//
// Arguments:	8(A6)	(input.W)		Install flag
//
// Called By:	A-trap through dispatcher
// Calls:		_WriteResource (later in purge proc) if a resource gets purged
//				and has been marked changed
//
// Function: 	Install or remove the resource manager purge hook in the
//				current zone.
//
//				Clearing now means leaving the disk cache purge proc in place.
//				Setting now means putting RPHook into the purge proc, and it
//				calls through to the the TrueType disk cache purge proc.		<sm10>stb
//_______________________________________________________________________________

    .globl DiskCachePurgeProcedure

//------ start of the resource mgr purge proc
// The actual purge hook code.  Assumes the handle is pushed on stack.
// Smashes A0, A1 and D0 always.
RPHook:

    move.l 4(%sp),-(%sp)
    jsr DiskCachePurgeProcedure // Give TrueType a crack at flushing this block	<sm10>stb
//  to disk if it‚Äôs a font cache block			<sm10>stb
    move.l (%sp)+, %a1 // Get the return address in A1
    move.l (%sp)+, %a0 // and the handle in A0.
    _hgetstate // D0 <- Lock/Purge/Res bits						<C169>
    btst #Resource,%d0 // NE => it‚Äôs indeed a rsrc						<C169>
    beq.w .L_RPHook_1 // If not resource, just return,
    move.l %a0, -(%sp) // otherwise push the handle
    _writeresource // Attempt a write (ResChanged checked)

.L_RPHook_1:
    jmp (%a1) // and return to memory manager
//------ end of the resource mgr purge proc

SetResPurge:

    move.l (%sp)+, %d0 // Get return address in D0
    tst.b (%sp)+ // Install or remove the hook?
    bne.w .L_SetResPurge_install // install

.L_SetResPurge_clear:
 // clearing means leaving the font disk cache purge proc in place			<sm10>stb
    lea DiskCachePurgeProcedure,%a1 // the TrueType purge procedure				<sm10>stb
    bra.w .L_SetResPurge_continue

.L_SetResPurge_install:
 // installing.  Put the resource mgr purge proc into the zone‚Äôs purgeProc
    lea RPHook,%a1 //												<sm10>stb

.L_SetResPurge_continue:

    move.l theZone,%a0 // Get the application zone						<18apr85> BBM<sm10>stb
    move.l %a1,purgeProc(%a0) // Set or clear the purge hook					<sm10>stb
    move.l %d0, -(%sp) // Push return address back
RsClrEx:
 // rts from SetResLoad, ResError & CurResFile	<13aug85> BBM
    clr.b ResOneDeep // disable one deep calls						<13aug85> BBM
    clr.w RomMapInsert // disable rom map next time 					<13aug85> BBM
    rts // and return.

//_______________________________________________________________________________
//
// Routine:		PROCEDURE SetResLoad(autoLoad:	BOOLEAN);
//
// Arguments:	4(SP)	(input.B)		autoload flag
//
// Called By:	A-trap through dispatcher
// Calls:		NONE
//
// Function: 	Set automatic-loading true or false.  If autoLoad=TRUE, then the
//				resource manager will load resources from the disk if necessary.
//				If FALSE, returns the handle, which may be empty.
//_______________________________________________________________________________

SetResLoad:

    move.l (%sp)+,%a0 // Get return address
    move.b (%sp)+,ResLoad // Set ResLoad from proc parameter
    move.l %a0,-(%sp) // Get return address							<13aug85> BBM
    bra.w RsClrEx // and return									<13aug85> BBM

//_______________________________________________________________________________
//
// Routine:		FUNCTION CurResFile:  INTEGER;
//
// Arguments:	4(SP)	(output.W)		refnum of current resource file
//
// Called By:	A-trap through dispatcher
// Calls:		NONE
//
// Function: 	Returns the reference number of the current resource file.
//_______________________________________________________________________________

CurResFile:

    move.w CurMap, 4(%sp) // else Set the function result as current map	<15jul85> BBM
    bra.w RsClrEx // and return									<13aug85> BBM

//_______________________________________________________________________________
//
// Routine:		FUNCTION ResError:	INTEGER;
//
// Arguments:	4(SP)	(input.L)		place to store ResError code
//				4(SP)	(output.L)		ResError code
//
// Called By:	A-trap through dispatcher
// Calls:		NONE
//
// Function: 	Return a resource error code.  Usually the code will be an I/O
//				error, but there are several resource-only errors which are
//				reported through this call.
//_______________________________________________________________________________

ResError:

    move.w ResErr,4(%sp) // Set error code as function result
    bra.w RsClrEx // and return									<13aug85> BBM

//_______________________________________________________________________________
//
// Routine:		PROCEEDURE AddReference(theResource: Handle; theID: INTEGER;
//										name: Str255);
//
// Arguments:	(output.W)				ResError code
//
// Called By:	A-trap through dispatcher
// Calls:		RsClrEx
//
// Function: 	Return a resource error code of AddRefFailed.
//_______________________________________________________________________________

AddReference:

    move.l (%sp)+,%a0 // get return address into A0					<17oct85> BBM
    adda.w #0xA,%sp // strip ten bytes off stack (parameters)		<17oct85> BBM
    move.l %a0,-(%sp) // push return address							<17oct85> BBM
    move.w #AddRefFailed,ResErr // Set error code as failed						<17oct85> BBM
    bra.w RsClrEx // and return									<17oct85> BBM
//_______________________________________________________________________________
//
// Routine:		PROCEEDURE RmveReference(theResource: Handle);
//
// Arguments:	(output.W)				ResError code
//
// Called By:	A-trap through dispatcher
// Calls:		RsClrEx
//
// Function: 	Return a resource error code of AddRefFailed.
//_______________________________________________________________________________

RmveReference:

    move.l (%sp)+,(%sp) // strip parameter with return address			<17oct85> BBM
    move.w #RmvRefFailed,ResErr // Set error code as failed						<17oct85> BBM
    bra.w RsClrEx // and return									<17oct85> BBM

//_______________________________________________________________________________
//
// Routine:		FUNCTION GetResAttrs(theResource:Handle): INTEGER;
//
// Arguments:	8(A6)	(input.L)		handle to the resource
//				(A0)	(input.L)		same as 8(A6)
//				12(A6)	(output.W)		resource attributes in low order byte
//
// Called By:	A-trap through dispatcher
// Calls:		StdEntry, RefHandle, StdFourExit
//
// Function: 	Given a resource, return the resource attributes in the low order
//				byte of the integer result.  If a reference to the resource exists
//				in the current resFile, returns attributes of that reference.
//_______________________________________________________________________________

GetResAttrs:

    bsr Std1Entry // Link A6, save regs							<22apr85> BBM
    move.l (%a0), %a1 // Get the resource handle
    bsr.w RefHandle // Scan for reference or handle
    bmi.w .L_GetResAttrs_9 // if not found, resErr set, just exit.

    move.b RAttr(%a2), 13(%a6) // Set function result in low byte of result

.L_GetResAttrs_9:
    bra StdFourExit // and branch to standard exit

//_______________________________________________________________________________
//
// Routine:		PROCEDURE SetResAttrs(theResource: Handle; attrs: INTEGER);
//
// Arguments:	10(A6)	(input.L)		handle to the resource
//				8(A6)	(input.W)		new attributes
//
// Called By:	A-trap through dispatcher
// Calls:		StdEntry, RefHandle, StdSixExit
//
// Function: 	Given a resource, set the resource attributes which are in the low
//		order byte of the integer parameter.  These new attributes will only
//		be in effect the next time the resource is loaded, except for SysHeap,
//		which will be ignored.	If a reference to the resource exists in the
//		current resource file, the attributes of the reference will be set.
//		<28jan85> BBM ram patches added in.
//_______________________________________________________________________________

SetResAttrs:

    bsr Std1Entry // Link A6, save regs							<22apr85> BBM
    move.l 10(%a6), %a1 // Get the resource handle
    bsr.w RefHandle // Look for a reference or the resource
    bmi.w .L_SetResAttrs_9 // if not found, ResErr set, just exit.
    cmp.l RomMapHndl, %a4 // is it in the ROM map? 						<28mar85> BBM
    beq.w .L_SetResAttrs_9 // then exit here too (just a nop)				<28mar85> BBM
    move.b 9(%a6), RAttr(%a2) // Set attr byte in the location longword
    move.l (%a4),%a0 // Dereference map handle						<28jan85> BBM
    bset #MapChanged,MAttr(%a0) // Set the map changed bit
    bsr UpdateOverriddenChangedBits // set the changed bit for any files that may be overridden by the map in A4

.L_SetResAttrs_9:
    bra StdSixExit // pop 6 bytes and exit

//_______________________________________________________________________________
//
// Routine:		RefHandle (utility)
//
// Arguments:	A1		(input.L)		handle to the resource.
//				Ax		(input.L)		assumes regs setup as per StdEntry
//
// Called By:	GetResAttrs, SetResAttrs, GetResInfo, SetResInfo
// Calls:		HandleScan
//
// Function: 	Scans for reference, then handle.  Returns MI if neither found,
//				and sets ResErr to ResNotFound.
//_______________________________________________________________________________

RefHandle:

    bsr HandleScan // Otherwise find the resource itself
    beq.w .L_RefHandle_9 // if resource found, exit, else
    move.w #ResNotFound, ResErr // Set ResErr, CC's
.L_RefHandle_9:
    rts // ...and return

//_______________________________________________________________________________
//
// Routine:		PROCEDURE GetResInfo(theResource:Handle; VAR theID:INTEGER;
//										VAR theType:LONGINT; VAR name:Str255);
//
// Arguments:	20(A6)	(input.L)		handle to res
//				16(A6)	(input.W)		Resource ID
//				12(A6)	(input.L)		Resource type
//				8(A6)	(input.L)		pointer to pascal string
//
// Called By:	A-trap through dispatcher
// Calls:		StdEntry, RefHandle, NameResource, CopyStr, StdExit
//
// Function: 	Given a resource, return the information about the resource in
//				the VAR parameters.  If a reference to the resource exists in the
//				current resFile, the information will be returned about the
//				reference.
//_______________________________________________________________________________

GetResInfo:

    bsr Std1Entry // Link A6, save regs							<22apr85> BBM
    move.l 20(%a6),%a1 // Get the resource handle
    bsr.w RefHandle // Look for a reference to this resource
    beq.w .L_GetResInfo_1 // If OK, go on, else ResErr set,
    lea MinusOne,%a2 // point to -1, -1,
    moveq #0, %d3 // and set type to null type.

.L_GetResInfo_1:
    lea 16(%a6),%a0 // Get top parameter address
    move.l (%a0),%a1 // Get address of the ID variable
    move.l %a1,%d0 // If pointer to var parameter is nil			<06may85> BBM
    beq.w .L_GetResInfo_2 // ... then don't write out.
    move.w RID(%a2),(%a1) // Store it
.L_GetResInfo_2:
    move.l -(%a0),%a1 // Get address of the type variable
    move.l %a1,%d0 // If pointer to var parameter is nil			<06may85> BBM
    beq.w .L_GetResInfo_3 // ... then don't write out.
    move.l %d3,(%a1) // Store it
.L_GetResInfo_3:
    move.l -(%a0),%a1 // Get address of the name variable
    move.l %a1,%d0 // If pointer to var parameter is nil			<06may85> BBM
    beq.w .L_GetResInfo_9 // ... then don't write out.
    clr.b (%a1) // Store first byte zero, for null string
    bsr NameResource // Given RNameOff(A2), point A0 to the name.
    bmi.w .L_GetResInfo_9 // If can't find the name, no go

    bsr.w CopyStr // Copy string in A0 to A1.
.L_GetResInfo_9:

    moveq #16,%d0 // Pop off 16 bytes of parameter
    bra StdExit // and exit.

//_______________________________________________________________________________
//
// Routine:		CopyStr (utility)
//
// Arguments:	A0		(input.L)		pascal String pointer
//				A1		(input.L)		pointer to new location
//
// Called By:	GetResInfo, AddName
// Calls:		_BlockMove
//
// Function: 	Given a Pascal string in A0 and an address in A1, copies the
//				string including the length byte to A1 using blockMove.
//_______________________________________________________________________________

CopyStr:

    moveq #0, %d0 // Zero high order bits.
    move.b (%a0), %d0 // Get length of string
    addq.w #1, %d0 // include length byte.							<14jun85> BBM
    _blockmove // Copy the string to A1
    rts // and return.

//_______________________________________________________________________________
//
// Routine:		PROCEDURE SetResInfo(theResource:Handle; theID:INTEGER;
//										theName:Str255);
//
// Arguments:	14(A6)	(input.L)		handle to the resource
//				12(A6)	(input.W)		resource ID
//				8(A6)	(input.L)		pointer to pascal string
//
// Called By:	A-trap through dispatcher
// Calls:		StdEntry, RefHandle, CheckGrow, RmveName, AddName, StdExit
//
// Function: 	Given a resource, Set the information about the resource.  If the
//				name pointer is NIL, doesn't change the name.  If the protect bit
//				is on, returns without doing anything.	If a reference to the
//				resource exists in the current resFile, the information about the
//				reference will be set.
//_______________________________________________________________________________

SetResInfo:

    bsr Std1Entry // Link A6, save regs							<22apr85> BBM
    move.l 14(%a6), %a1 // Get the resource handle
    bsr.w RefHandle // Look for a reference to this resource
    bmi.w .L_SetResInfo_9 // Couldn't find it, or reference!
    btst #ResProtected,RAttr(%a2) // Is it protected from being changed?
    bne.w .L_SetResInfo_5 // Yes, return immediately, otherwise			<16apr85> BBM

    move.l 8(%a6),%d0 // Check for a nil name pointer					<22aug85> BBM
    beq.w .L_SetResInfo_1 // if no name just change RID
    sub.l %a1, %a1 // (no handle)
    bsr CheckGrow // can the resfile + map grow? (sets ResErr)
    bmi.w .L_SetResInfo_9 // if failed, can't set.  Bad boundary condition.

    move.l %d0, %a0 // Get name pointer in A0
    bsr RmveName // Remove old name, if it exists
    bsr AddName // Add the new one back, Sets in RNameOff(A2)
    bsr SetRFEOF // set new end of file							<26aug85> BBM

.L_SetResInfo_1:
    move.l (%a4),%a0 // Dereference map handle						<22aug85> BBM
    bset #MapChanged,MAttr(%a0) // set the mapchanged bit
    bsr UpdateOverriddenChangedBits // set the changed bit for any files that may be overridden by the map in A4
    move.w 12(%a6), RID(%a2) // Get new ID and store it
    bra.w .L_SetResInfo_9 // skip over error
.L_SetResInfo_5:
    move.w #resAttrErr,ResErr // ... else mark it as error 					<16apr85> BBM
.L_SetResInfo_9:
    moveq #10, %d0 // pop off 10 bytes of parameters
    bra StdExit // Restore regs, unlink A6

//_______________________________________________________________________________
//
// Routine:		PROCEDURE AddResource(theResource:Handle; theType:LONGINT;
//										theID:INTEGER; name:Str255);
//
// Arguments:	18(A6)	(input.L)		handle to the resource
//				14(A6)	(input.W)		theType
//				12(A6)	(input.W)		theID
//				8(A6)	(input.L)		pointer to pascal string
//
// Called By:	A-trap through dispatcher
// Calls:		StdEntry, HandleScan, GetCurMap, CheckGrow, AddNewRef, AddName,
//				AllocRes, StdExit
//
// Function: 	Add a reference to the given resource in the current map.  Set ID
//				and name in the reference.	Mark the resource to be written out
//				into the correct resource map data section.
//_______________________________________________________________________________

AddResource:


    move.l ExpandMem,%a0 //
    move.b emScanOverrideMaps(%a0),%d0 // save override state. (StdZEntry must preserve d0)
    clr.b emScanOverrideMaps(%a0) // turn it off for StdEntry

    bsr StdZEntry // Link A6, save regs		<12aug85> BBM
    bsr FlushResourceCache // <PN> need to flush the resource cache
    move.l ExpandMem,%a0 //
    move.b %d0,emScanOverrideMaps(%a0) // restore override state

    move.w #AddResFailed,ResErr // Assume error.  CheckGrow resets ResErr.
    move.l 18(%a6), %a1 // Get handle in A1
    move.l %a1, %d0 // Set CC's
    beq.w .L_AddResource_9 // if NIL handle, can't add!
    bsr HandleScan // Is the data already a resource?
    beq.w .L_AddResource_9 // cann't add another                            <21may85> BBM
    bsr GetCurMap // Get the current map for diskFull check
    bsr CheckGrow // Room for resource + map overhead?
// CheckGrow calls ResRW which tests MapReadOnly <28mar85> BBM
    bmi.w .L_AddResource_9 // if not, can't add the resource.
// Return with error from CheckGrow.
// If successful, ResErr := 0.

    move.l 14(%a6), %d3 // Put type of resource to add in D3
    bsr.w AddNewRef // Set up a new reference to the resource
// Returns A2-->reference location.
    move.w 12(%a6),(%a2)+ // Set ID of reference
    move.w #-1,(%a2)+ // Assume no name yet
    move.l #0x02000000,(%a2)+ // ResChanged set
    move.l 18(%a6), %a0 // Get the handle
    _hsetrbit // Mark it like a gentleman		<C169>
    move.l %a0, (%a2) // Finally, set handle.
    subq.l #8, %a2 // Back up for AddName, AllocRes.

    move.l 8(%a6), %a0 // Get pointer to reference name
    bsr AddName // Name set in RNameOff(A2).
// A2, A3 preserved + correct.
    bsr AllocRes // Allocate new resource loc'n.
    bsr FlushChangedFONDs // flush 'FOND' candidate list if this is a FOND	<sm 5> stb
.L_AddResource_9:
    moveq #14, %d0 // Pop off 14 bytes of parameter
    bra StdExit // Restore regs, unlink A6

//_______________________________________________________________________________
//
// Routine:		AddNewRef (utility)
//
// Arguments:	A4		(input.L)		handle to map
//				A3		(input.L)		pointer into map
//				D3		(input.L		type of resource to add
//				A2		(output.L)		pointer to loc of new reference
//
// Called By:	AddResource
// Calls:		TypeScan, TypeScanAll, ShiftBlock, MoveNames, GetTypes,
//				SkipToEntry
//
// Function: 	add a new reference in the map (of type specified in D3)
//_______________________________________________________________________________

AddNewRef:

    bset #MapChanged, MAttr(%a3) // Set mapChanged bit
    bsr UpdateOverriddenChangedBits

AddNewRefWithoutUpdate:
    bsr TypeScan // Scan for type of referent (D3) in cur map
    beq.w .L_AddNewRefWithoutUpdate_2 // If found, type exists, otherwise

// Add both a new type and a new entry.

    moveq #RESize+TSize, %d0 // Move names up by RE+TE size to leave
    bsr MoveNames // room in handle for type and res entry.

    bsr GetTypes // Point to first type, if any.
    bmi.w .L_AddNewRefWithoutUpdate_1 // If none, skip SkipToentry (adding 1st one!)
    bsr SkipToEntry // ... skip to res entry.  A2-->first res entry.

.L_AddNewRefWithoutUpdate_1:
 // start adding type here (A2 should point at loc where to add new type)

    move.l %a2, %a3 // A3 is location of new type entry.
    move.l (%a4), %a2 // Dereference map handle.
    add.w MNames(%a2), %a2 // A2 points to new res entry loc'n.
    sub.w #RESize+TSize, %a2 // (name block already moved.)

    moveq #TSize, %d0 // Now shift all entries up by TSize
    move.l %a3, %a0 // (A3=first entry loc)
    move.l %a2, %a1 // new res entry locn is high bounds of block.
    bsr ShiftBlock // Made room for type entry now.

    move.l (%a4), %a0 // Dereference map handle
    add.w MTypes(%a0), %a0 // Point to type block
    addq.w #1, (%a0) // Increment type count.

    move.l %d3, (%a3)+ // Set the new type
    clr.w (%a3)+ // Set TCount to 0 (1 entry)
    sub.l %a0, %a1 // Subtract first entry loc from block start
    move.w %a1, (%a3) // to get the offset (new entry needs +TSize too).
    addq.l #TSize, %a2 // add offset to returned loc'n
    move.w (%a0)+, %d5 // Get number of types
    bra.w UTOffsets // update ALL type offsets by TSize and exit.

// The type already exists.	Just add a new entry.
// This part only adds the new entry
.L_AddNewRefWithoutUpdate_2:

    moveq #RESize, %d0 // Move names up by entry size only.
    bsr MoveNames // Move names up for room

    move.l (%a4), %a1 // Dereference map handle.
    add.w MNames(%a1), %a1 // end of block to move is beginning of names.
    sub.w %d0, %a1 // names moved already--skip back to old val.

    bsr TypeScan // Scan for type of referent (D3) in cur map

    movem.l %a0/%d0,-(%sp)
    pea .Ldbg_anr3(%pc)
    move.l (%sp)+,%a0
    move.l %a2,%d0
    .word 0xA0FE               /* trace: after 2nd TypeScan, A2=type entry */
    movem.l (%sp)+,%a0/%d0

    move.l %a2, -(%sp) // Save type entry location for offset updating

    addq.w #1,TCount(%a2) // Increment entry count for this type
    moveq #RESize,%d0 // get offset to last Rsrc entry in D0			<14sep85> BBM
    mulu TCount(%a2),%d0 // D0 has offset past the first entry			<14sep85> BBM
    move.w TOffset(%a2),%a2 // Skip to the first entry of that type
    add.l %a3,%a2 // add in block address gives absolute addr.
    add.w %d0,%a2 // point to the last entry						<14sep85> BBM
    move.l %a2,%a0 // Start moving entries from here to names.
    moveq #RESize,%d0 // Move entries up by entry size only.
    bsr ShiftBlock // shift block up by RESize.

    move.l (%sp)+, %a0 // Restore type entry location, and update
// following types by RESize.
    bra.w UTInLoop // jump in lp to skip type entry

//_______________________________________________________________________________
//
// Routine:		UTOffsets, UTInLoop (utilities)
//
// Arguments:	D0		(input.W)		Delta offset for this type
//				D5		(input.L)		types-1 remaining
//				A0		(input.L)		points to first entry
//
// Called By:	(UTOffsets): AddNewRef, RmveCmn
//				(UTInLoop): AddNewRef
// Calls:		NONE
//
// Function: 	Update type offsets
//_______________________________________________________________________________

UTOffsets:

    add.w %d0, TOffset(%a0) // Increment by D0
UTInLoop:

    addq.w #TSize, %a0 // bump address
    dbra %d5, UTOffsets // Update each name offset
    rts

//_______________________________________________________________________________
//
// Routine:		PROCEDURE RmveResource(theResource: Handle);
//				RmveCmn (utility)
//
// Arguments:	8(A6)	(input.L)		handle to the resource
//				(A0)	(input.L)		handle to the resource
//
// Called By:	(RmveResource): A-trap through dispatcher
//				(RmveCmn): RmveResource (by falling through)
// Calls:		(RmveResource): StdEntry, HandleScan, RmveExit
//				(RmveCmn): RmveName, TypeScan, TypeScanALl, ShiftBlock, UTOffsets,
//							MoveNames, StdFourExit
//
// Function: 	Remove a resource from the map, and from the data section of the
//				resource file when UpdateResFile/CloseResFile is called.  May
//				only be removed if it is in current map and is not protected.
//_______________________________________________________________________________

RmveResource:

    bsr Std1Entry // Link A6, save regs							<22apr85> BBM
    bsr FlushResourceCache // preserves a0
    move.l (%a0), %a1 // Get the handle in A1
    bsr HandleScan // Find the handle, wherever it is.
    bmi RmveErr // Can't find it, so can't remove it.			<SM51> CSS
    bsr FlushChangedFONDs // flush 'FOND' candidate list if this is a FOND	<sm 5> stb
    btst #ResProtected, RAttr(%a2) // Is it protected?
    bne RmveErr // Yes, return immediately						<SM51> CSS

// RmveResource is documented to only allow you to remove a resource
// when it's homeresfile is in CurMap. With overrides we want to modify
// this behavior slightly to allow RmveResource calls from CurMap
// and any files that are overridden by CurMap.

    cmp.w CurMap, %d6 // Is it in the current map?
    beq.w RmveIt // If so, go ahead and remove it‚Ä¶

    subq #4,%sp
    move.l %a4,-(%sp)
    _getoverridemap // Get the override map for the map we found the resource in.
    movea.l (%sp)+,%a3 // Hold the override map in a3 the link we're looking for

    move.w CurMap,%d1 // Get CurMap
    move.l (%a3),%a3 // Get Override map ptr in a3
    cmp.w mRefNum(%a3),%d1 // were we removing from an overridden file?
    beq.w RmveIt

// It wasn't an overridden file so we should check for "two deep bit" and other
// system files if the current map is the override of the system map.

// <SM51> CSS changed to look at the override map to the system not SysMap!

    subq #4,%sp
    move.l SysMapHndl,-(%sp)
    _getoverridemap // Get the override map for the system
    movea.l (%sp)+,%a3 // hold the override map in a3 of the system
    move.w CurMap,%d1 // Get CurMap
    move.l (%a3),%a3 // get override ptr in a3
    cmp.w mRefNum(%a3),%d1 // is the system override map the curmap?
    bne RmveErr // no - bail out

    subq #2,%sp
    move.l %a4,-(%sp)
    _isthisasystemresourcemap // See if this map is a font file or other two deep bit file‚Ä¶
    tst.b (%sp)+
    beq.w RmveErr // error out‚Ä¶									<SM51> CSS

RmveIt:

    movea.l %a1,%a0 // Mark it no longer a rsrc			<C169>
    _hclrrbit //									<C169>
    moveq #MCCMask, %d0 // MapChanged+MapCompact to be set in RmveCMn
    bsr UpdateOverriddenChangedBits // set the changed bit for any files that may be overridden by the map in A4

// Common code for remove reference/remove resource.
// A2 points to the resource entry.

RmveCmn:

    clr.l HSCHandle // invalidate cache always						<18jul85> BBM
    move.l (%a4), %a0 //
    or.b %d0, MAttr(%a0) // Set mapChanged and possibly mapCompact.
    bsr RmveName // Remove RNameOff(A2)'s name, if any.

    moveq #-RESize, %d7 // Get -(resource entry size)
    move.l %a2, %a0 // Remember address of
    sub %d7, %a0 // NEXT entry, in A0.

    bsr TypeScan // Scan for type of referent (D3) in cur map

    move.l (%a4), %a1 // Dereference map handle
    adda.w MNames(%a1), %a1 // A1 points to the name block

    move.l %d7, %d0 // Copy (-RESize) in D0
    bsr ShiftBlock // Shift the block down

    move.l %a2, %a0 // Copy type entry address to A0
    addq #TSize, %a0 // Skip to next type entry
    subq #1, %d5 // Decrement number of types
    bmi.w .L_RmveCmn_1 // If this is last type, go on, else
    bsr.w UTOffsets // update following type offsets.

.L_RmveCmn_1:
    subq.w #1, TCount(%a2) // decrement resource count for this type
    bpl.w .L_RmveCmn_9 // If still more resources of this type, quit

// If this was the last resource of the type, remove the type entry.

    addq #TSize, %a2 // Skip to next type entry
    move.l %a2, %a0 // Moving next type over current, to delete.
    add %d0, %a1 // move down name address by resource entry size
    moveq #-TSize, %d0 // Shift this block down by type entry size
    add.l %d0, %d7 // Decrement down TSize more, for names.
    bsr ShiftBlock
    move.l (%a4), %a0 // Dereference map handle
    adda.w MTypes(%a0), %a0 // A2 points to the type block
    subq #1, (%a0) // Decrement number of types
    move.w (%a0)+, %d5 // Get number of types in D5
    bmi.w .L_RmveCmn_9 // No types left!  just return
    bsr UTOffsets // Update all type offsets by TSize.

.L_RmveCmn_9:
    move.l %d7, %d0 // Get accumulated shuffle in D0.
    bsr.w MoveNames // Move names down 12 or 20

    cmp.w #1,%d6 // IS THIS THE ROM MAP?? 						<04nov85> BBM
    beq.w .L_RmveCmn_5 // CC=EQ means ROM map (don't do SetEOF)         <04nov85> BBM

    bsr SetRFEOF // set new EOF									<26aug85> BBM

.L_RmveCmn_5:
    clr.w ResErr // Remove succeeded! 							<04nov85> BBM

RmveExit:

    clr.l HSCHandle // Invalidate cache								<12mar85> BBM
    bra StdFourExit

//<SM51> CSS added RmveErr label so that we don't have to keep loading ResErr
RmveErr:

    move.w #RmvResFailed,ResErr // indicate error doing remove
    bra.w RmveExit // and exit

//_______________________________________________________________________________
//
// Routine:		AddName (utility)
//
// Arguments:	A0		(input.L)		pointer to a name
//				A2		(input.L)		pointer to a resource entry
//
// Called By:	SetResInfo, AddResource
// Calls:		ResizeMap, CopyStr
//
// Function: 	Sets RNameOff(A2) to a name offset, and copies the name to the
//				map block.
//_______________________________________________________________________________

AddName:

    clr.l HSCHandle // invalidate cache always						<18jul85> BBM
    movem.l %d0-%d2/%a0-%a1, -(%sp) // save regs used (returning D2), and
    moveq #-1, %d2 // Assume no string

    move.l %a0,%d0 // is the ptr nil?								<13> stb
    beq.w .L_AddName_9 // if so, return -1 for RNameOff

    tst.b (%a0) // is it empty?
    beq.w .L_AddName_9 // if so, return -1 for RNameOff.
    move.l (%a4), %a1 // Dereference map handle
    move.l MapSize(%a1), %d1 // Copy current map size to D1.
    move.l %d1, %d2 // and to D2.

    moveq #0, %d0 // Zero D0
    move.b (%a0), %d0 // Get string length
    addq.l #1, %d0 // Add 1 for len byte
    bsr.w ResizeMap // Grow the map by string size.

    moveq #0,%d0 // use all of D0 								<06may85> BBM
    move.l (%a4), %a1 // Get map ptr again
    move.w MNames(%a1), %d0 // D0=offset to name block
    sub.l %d0, %d2 // mapSize-nameBlkoff := off to newname			<06may85> BBM
    add.l %d1, %a1 // Add endOfMap to get end address in A1
    bsr CopyStr // Copy the string in!

.L_AddName_9:
    move.w %d2, RNameOff(%a2) // Set name offset in the entry
    movem.l (%sp)+, %d0-%d2/%a0-%a1 // Restore regs
    rts // and exit.

//_______________________________________________________________________________
//
// Routine:		MoveNames (utility)
//
// Arguments:	D0		(input.L)		byte count
//				A2		(input.L)		pointer to map (see note below)
//				A3		(input.L)		pointer into map (see note below)
//				A4		(input.L)		handle to the map
//
// Called By:	AddNewRef, RmveCmn
// Calls:		ResizeMap, ShiftBlock
//
// Function: 	Moves the names up or down by the amount specified in D0.
//				Resizes map each time.
//
// note: 		Adjusts A3, A2 to compensate for any block moving.
//_______________________________________________________________________________

MoveNames:

    movem.l %d0-%d1/%d7/%a0-%a1, -(%sp) // Save regs
    move.l (%a4), %a0
    move.l MapSize(%a0), %d1 // Save original map size in D1
    tst.l %d0 // Are we shrinking or growing?
    bmi.w .L_MoveNames_1 // if Shrinking, resize after move
    bsr.w ResizeMap // Otherwise grow first
    move.l (%a4), %a0 // Dereference handle (may have moved)
.L_MoveNames_1:

    move.l %a0,%a1 // Copy address
    moveq #0,%d7 // Get MNames(A0) into D7 with high word = 0 	<06may85> BBM
    move.w MNames(%a0),%d7 // get offset to names
    adda.l %d7,%a0 // and add offset to pointer.
    add.w %d0,MNames(%a1) // (Update name block offset now)
    add.l %d1,%a1 // Point A1 to original end of map
    bsr.w ShiftBlock // Shift (A0..A1) up or down by D0
    tst.l %d0 // Did we shrink or grow?
    bpl.w .L_MoveNames_9 // Grew, map already resized, otherwise
    bsr.w ResizeMap // Shrink it now

.L_MoveNames_9:
    movem.l (%sp)+, %d0-%d1/%d7/%a0-%a1 // Restore regs
    rts // ...and return

//_______________________________________________________________________________
//
// Routine:		ResizeMap (utility)
//
// Arguments:	D0		(input.L)		delta byte count
//				A2		(input.L)		pointer to map (see note below)
//				A3		(input.L)		pointer into map (see note below)
//				A4		(input.L)		handle to the map
//
//
// Called By:	MoveNames, AddName, RmveName
// Calls:		_SetHandleSize, SetRFEOF
//
// Function: 	Given a byte count in D0, grows or shrinks the map.  The map may
//				move when growing, though not when shrinking.  Calls SetRFEOF to
//				set resource file EOF (reserving space).
//
// note: 		Adjusts A3, A2 to compensate for any block moving.
//_______________________________________________________________________________

ResizeMap:

    bsr SaveRegs // Saves A1/A0/D1/D0 and makes A2/A3 offsets 	<22aug85> BBM
    move.l (%a4), %a0 // Dereference handle
    add.l MapSize(%a0), %d0 // Add delta to current size for new size_
    move.l %d0, MapSize(%a0) // Set new mapSize.
    move.l %a4, %a0 // Get handle in A0
    _sethandlesize // Set the new size (error handled in CheckGrow)
    bra RestRegs // Set EOF and exit through SetEOF2				<26aug85> BBM

//_______________________________________________________________________________
//
// Routine:		ShiftBlock (utility)
//
// Arguments:	D0		(input.L)		delta byte count
//				A0		(input.L)		low pointer to block to move
//				A1		(input.L)		high pointer to block to move
//
// Called By:	AddNewRef, RmveCmn, MoveNames, RmveName
// Calls:		_BlockMove
//
// Function: 	Given a delta (in bytes) in D0, shifts the block (A0..A1) up or
//				down by D0.
//_______________________________________________________________________________

ShiftBlock:

    movem.l %a0-%a1/%d0, -(%sp) // Save regs
    exg %d0, %a1 // D0=bytes to move, A1=delta
    sub.l %a0, %d0 // Subtract low from high to give size
    and.l Lo3Bytes,%d0 // AND off high 8 bits
    add.l %a0, %a1 // Add low to bytes to move, giving dest.
    _blockmove // Move the bytes
    movem.l (%sp)+, %a0-%a1/%d0 // Restore regs
    rts // ...and return
//_______________________________________________________________________________
//
// Routine:		NameUsedByOthers (utility)									<11> stb
//
// Arguments:	D0		(input.L)		offset to name in name list
//				A2		(input.L)		resource entry of interest
//
// Called By:	RmveName
// Calls:
//
// Function: 	Given an offset to a name in the namelist, return non-zero if
//				any resource other than the resource of interest uses the same name
//				offset.  While this shouldn‚Äôt ever be the case, such files do exist,
//				and we‚Äôll make them work.
//_______________________________________________________________________________

NameUsedByOthers:

    movem.l %d3-%d5/%a2-%a3,-(%sp) // Save rest of registers.
//		MOVE.L	A2,A3					; A3 is now the resource of interest	<LW15>
    move.l %a2,-(%sp) // Save A2								<LW15>
    bsr GetRsrcCnt // Check all entries and update name offsets.
// A2 now points to the first rsrc entry
// D4 has the number of entries
    move.l (%sp)+,%a3 // A3 is now the resource of interest	<LW15>
.L_NameUsedByOthers_loop:

    cmp.l %a2,%a3 // current resource vs. resource of interest
    beq.w .L_NameUsedByOthers_NextEntry // skip this one, we already know it uses the name
    cmp.w RNameOff(%a2),%d0 // is this name offset = the one of interest?
    beq.w .L_NameUsedByOthers_NameInUse // yes?  return non-zero

.L_NameUsedByOthers_NextEntry:

    add.w #RESize,%a2 // point to the next entry
    dbra %d4,.L_NameUsedByOthers_loop // if there are more entries loop back
    moveq #0,%d0 // zero indicates not in use
    bra.w .L_NameUsedByOthers_done

.L_NameUsedByOthers_NameInUse:

    moveq #1,%d0 // non-zero indicates in-use

.L_NameUsedByOthers_done:

    movem.l (%sp)+,%d3-%d5/%a2-%a3 // Restore inner regs.
    rts // ...and return

//_______________________________________________________________________________
//
// Routine:		RmveName (utility)
//
// Arguments:	A2		(input.L)		pointer to resource entry
//				A3		(input.L)		pointer into map
//				A4		(input.L)		handle to map
//
// Called By:	SetResInfo, RmveCmn
// Calls:		ShiftBlock, ResizeMap, GetEntries
//
// Function: 	Given A2 pointing to a resource entry, removes the name pointed
//				to by RNameOff(A2).  Removing reference names requires that A2
//				point to RefID instead of RID.
//_______________________________________________________________________________

RmveName:

    movem.l %d0-%d2/%a0-%a1,-(%sp) // Save working regs
    move.w RNameOff(%a2),%d2 // Copy name offset to D2.
    bmi.w .L_RmveName_9 // if not a real offset, just exit.

    move.l %d2,%d0 // offset into namelist									<11> stb
    bsr.w NameUsedByOthers // Is anyone else is using this instance of the name?	<11>
    bne.w .L_RmveName_9 // It‚Äôs in use.  Don‚Äôt remove the name					<11>

    move.l (%a4),%a0 // Dereference map handle
    move.l %a0,%a1 // Copy ptr to A1
    moveq #0,%d0 // Get name offset (upto 64k)					<06may85> BBM
    move.w MNames(%a0),%d0 // ... and stuff into low word
    add.l %d0,%a0 // A0 points to the name block
    add.w %d2,%a0 // and finally the string itself.
    adda.l MapSize(%a1),%a1 // A1 points to the end of the map.

    moveq #0, %d0 // Zero high order
    move.b (%a0), %d0 // Get length byte
    addq.l #1, %d0 // Add 1 to include the length byte
    add.l %d0, %a0 // point A0 to the NEXT string
    neg.l %d0 // Negate, since shrinking
    move.l %d0, %d1 // Copy delta to D1 for updating offsets.
    bsr.w ShiftBlock // Shift the block (A0..A1) down strSize.
    bsr.w ResizeMap // and shrink the map. A2-A3 corrected.

    movem.l %d3-%d5/%a2-%a3,-(%sp) // Save rest of registers.						<24aug85> BBM
    bsr GetRsrcCnt // Check all entries and update name offsets.	<24aug85> BBM
.L_RmveName_1:
    cmp.w RNameOff(%a2),%d2 // is this name offset < the removed one?		<24aug85> BBM
    bge.w .L_RmveName_2 // if so, skip it, else
    add.w %d1,RNameOff(%a2) // Offset shrinks by D1.
.L_RmveName_2:
    add.w #RESize,%a2 // point to the next entry
    dbra %d4,.L_RmveName_1 // if there are more entries loop back			<24aug85> BBM
    movem.l (%sp)+,%d3-%d5/%a2-%a3 // Restore inner regs.							<24aug85> BBM

.L_RmveName_9:
    movem.l (%sp)+, %d0-%d2/%a0-%a1 // Restore outer regs
    move.w MinusOne, RNameOff(%a2) // Set name offset to -1.
    rts // and return.

//_______________________________________________________________________________
//
// Routine:		FUNCTION SizeResource(theResource:	Handle): LONGINT;
//
// Arguments:	8(A6)	(input.L)		resource handle
//				A0		(input.L)		resource handle
//				12(A6)	(output.L)		resource size
//
// Called By:	A-trap through dispatcher
// Calls:		StdResEntry, _GetHandleSize, SizeRsrc, StdFourExit
//
// Function: 	Return the size of the resource pointed at by <theResource>.  If
//				no resource was found for <theResource>, return (-1).
// note:
// <22Jan85> BBM This was first includeded in the Ram System in 1984.  It was not
//				put in ROM 7.0. This routine will return the size of a resource.
//_______________________________________________________________________________

SizeResource:

    bsr Std2ResEntry // Link A6, save regs							<21aug85> BBM
    tst.l (%a1) // has it been purged?
    beq.w .L_SizeResource_0 // if so, read size from file

    move.l %a1,%a0 // Resource is already in memory.
    _gethandlesize // (handle to resource is in A0)
    bra.w SizeRExit // EXIT

.L_SizeResource_0:
    move.b RAttr(%a2),-(%sp) // Rsrc not in memory, save state of ...
    bsr SizeRsrcDecomp // attributes. Figure out the size on disk after decompression. <sm21>kc
    move.b (%sp)+,RAttr(%a2) // restore attributes state.
SizeRExit:
 // added common exit for MaxsizeRsrc 			<21aug85> BBM
    move.l %d0,12(%a6) // Return the result and ...
    bra StdFourExit // Pop 4 bytes and standard exit

//_______________________________________________________________________________
//
// Routine:		FUNCTION MaxSizeRsrc(theResource:  Handle): LONGINT;
//
// Arguments:	8(A6)	(input.L)		resource handle
//				A0		(input.L)		resource handle
//				12(A6)	(output.L)		resource size
//
// Called By:	A-trap through dispatcher
// Calls:		StdResEntry, SpaceAt, _GetHandleSize, SizeRExit
//
// Function: 	Return the maximum size of the given resource.	Note
//				no resource was found for <theResource>, return (-1).
//_______________________________________________________________________________

MaxSizeRsrc:
 //												<02may85> BBM
    bsr Std2ResEntry // Link A6, save regs							<21aug85> BBM
    tst.l (%a1) // has it been purged?
    bne.w .L_MaxSizeRsrc_0 // if CC=EQ, read size from Map
    bsr SpaceAt // Get the Size available						<13aug85> BBM
    bra.w .L_MaxSizeRsrc_9 // EXIT
.L_MaxSizeRsrc_0:
    move.l %a1,%a0 // Resource is already in memory.
    _gethandlesize // (handle to resource is in A0)
.L_MaxSizeRsrc_9:
    bra.w SizeRExit // write out result and exit 					<21aug85> BBM

//_______________________________________________________________________________
//
// Routine:		FUNCTION RsrcMapEntry(theResource:	Handle): LONGINT;
//
// Arguments:	8(A6)	(input.L)		resource handle
//				A0		(input.L)		resource handle
//				12(A6)	(output.L)		resource entry offset
//
// Called By:	A-trap through dispatcher
// Calls:		StdResEntry, SizeRExit
//
// Function: 	Return the offset in the map to the resource entry.
//_______________________________________________________________________________

RsrcMapEntry:
 //												<21aug85> BBM
    bsr Std2ResEntry // Link A6, save regs
    move.l %a2,%d0 // Get pointer to rsrc entry
    sub.l (%a4),%d0 // sub pointer to beginning of map
    _stripaddress // get rid of any masterpointer bits				<c396>
.L_RsrcMapEntry_9:
    bra.w SizeRExit // write out result and exit 					<21aug85> BBM

//_______________________________________________________________________________
//
// Routine:		FUNCTION rGetResource(theType: ResType; theID: INTEGER): Handle;		<C628>
//
// Arguments:	10(A6)	(input.L)		resource type
//				8(A6),A0(input.W)		resource ID
//				14(A6)	(output.L)		resource handle
//
// Called By:	A-trap through dispatcher
// Calls:		GetResource
//
// Function: 	do a normal GetResource without the rom map; if that fails,
//				do GetResource with the rom map inserted.
//
// NOTE:			_GetResource expects a specific items on the stack when called.
//				This routine depends heavily on that stack.  Below is a picture
//				of the stack just before the _GetResource trap is executed
//				(starting in the middle of the stack and ending at the top of stack,
//				or starting in high memory and decending down in memory).
//
//				handle		4 bytes
//				type		4 bytes		(E bytes back on stack after < subq #4,sp >)
//				ID			2 bytes		(C bytes back on stack after < subq #4,sp >)
//				rtn addr	4 bytes		(8 bytes back on stack after < subq #4,sp >)
//				rtn addr	4 bytes		(4 bytes back on stack after < subq #4,sp >)
//				handle		4 bytes		(0 bytes back on stack after < subq #4,sp >)
//				type		4 bytes
//				ID			2 bytes
//_______________________________________________________________________________
//‚Ä¢‚Ä¢‚Ä¢ Do not roll in patch from system file into this routine <PN> ‚Ä¢‚Ä¢‚Ä¢

    .equ grStFr, 0x0E // size of the stack frame. (see note above)

rGetResource:

    clr.b ROMMapInsert // make sure we don‚Äôt use rom map
    bsr.w .L_rGetResource_getrsrc //
    bne.w .L_rGetResource_exit // if handle is not zero, then we don‚Äôt check rom

    st.b ROMMapInsert // else make sure we use rom map
    bsr.w .L_rGetResource_getrsrc //

.L_rGetResource_exit:
 // std exit to strip the stack
    move.l (%sp)+,%a0 // strip off the return address into a0				<C669>
    addq #6,%sp // strip off type and ID
    jmp (%a0) // and return through a0

.L_rGetResource_getrsrc:
 // go do GetResource
    move.b resload,ROMMapInsert+1 // set temp value of resload to current resload
    subq #4,%sp // save room for another handle (see note above)
    move.l grStFr(%sp),-(%sp) // push type again
    move.w grStFr+2(%sp),-(%sp) // push ID again
    _getresource //
    move.l (%sp)+,grStFr(%sp) // push the handle we got back into the area
// the user saved for the handle and set cc‚Äôs
    rts //




//=== Here‚Äôs some new stuff as of 3/11/92 ================================================
//_______________________________________________________________________________
// CacheResource builds the list of resources that are actually counted or indexed.  Calls
// to _AddResource and _RmveResource will flush this cache.  The cache contents look like
// this:
//
//	Offset		Contents
//		0		Cache flags
//		2		File reference number
//		4		Resource type in cache
//		8		Number of entries in cache - 1
//		12		Beginning of cache information
//			0	Handle to resource map containing this resource						<31>
//			4	Offset from beginning of map to this resource (word)				<31>
//			6	ID of this resource (word)											<29>
//
// The cache flags word is a bit field describing the state of the Resource Manager
// when the cache was formed.  The cache will be flushed if the current state of the
// Resource Manager does not match the state when the cache was formed.  The following
// bits are currently defined for the cache flags:
//
//	(0) kCacheIsOneDeepBit			Cache was formed on a _Count1Resources or _Get1IxResource call.
//	(1) kCacheHasROMResourcesBit	Cache was formed when ROMMapInsert was true.
//	(2) kCacheHasOverridesBit		Cache was formed when emScanOverrideMaps was true.
//

CacheResource:


    move.l (%sp)+,%a0 // Get return address
    move.l (%sp)+,%d0 // Get resource type
    move.l %a0,-(%sp) // Restore return address
    movem.l %d2-%d7/%a1-%a4,-(%sp) // Save registers
    move.l %d0,%d7 // Keep type in a nonvolatile place

    move.l ExpandMem,%a0
    move.b emScanOverrideMaps(%a0),%d3 // <35> Get scan override state
    beq.w .L_CacheResource_overridingNotOn // <35> Why would anyone want to do this?
    move.w #kCacheHasOverridesMask,%d3 // <35> Set to a known value, since Pascal would have put a 1 in emScanOverrideMaps, whereas C would have put in -1, and who knows what Asm would have done‚Ä¶
.L_CacheResource_overridingNotOn:

    move.l emResourceCache(%a0),%d0 // Get cached resource from expanded low mem
    beq.w .L_CacheResource_setupCache // Cache doesn‚Äôt exist.  Go build it
    move.l %d0,%a0

//
// <7> See if the deepness of the cache matches that of ResOneDeep.  If not, force the
//		cache to be rebuilt.
// <34> Also make sure that ROMMapInsert hasn‚Äôt changed since the cache was formed.
//

    move.l (%a0),%a0
    move.b ResOneDeep,%d1 // Get one deep setting of current call
    andi.w #kCacheIsOneDeepMask,%d1 // Just keep the low bit
    move.b ROMMapInsert,%d2 // <34> Check ROMness of cache too
    andi.w #kCacheHasROMResourcesMask,%d2 // <34> Just get a bit so we can compare against the cache flags
    or.w %d1,%d2 // <34> Merge deepness and ROMness
    or.w %d3,%d2 // <35> Merge overrideness in with the other two
    cmp.w (%a0)+,%d2 // Does the deepness of the cache match ResOneDeep?
    bne.w .L_CacheResource_deepnessDoesntMatch // Yes.
    btst #kCacheIsOneDeepBit,%d2 // <11> <23> Check the deepness of the current call
    beq.w .L_CacheResource_mapMatches // <11> If the call is not one deep, go ahead and use the cache
    move.w CurMap,%d1 // <10>
    cmp.w (%a0)+,%d1 // <10> One deep from same map?
    beq.w .L_CacheResource_mapMatches // <10> Yes
    move.w %d1,-2(%a0) // <11> CurMap has changed.  Remember the new one for next time
.L_CacheResource_deepnessDoesntMatch:

    clr.l (%a0) // Nail type to force cache to rebuild
.L_CacheResource_mapMatches:

    move.l %d0,%a0 // Get handle
    _gethandlesize
    sub.l #kCacheHeaderSize,%d0 // Don‚Äôt count header information
    asr.l #3,%d0 // <31> Figure out how many things can fit in the existing handle
    move.l %d0,%d6 // Save this count
    move.l %a0,%a3
    move.l (%a3),%a0
    move.w %d2,(%a0)+ // <34> Save deepness, ROMness, and overrideness in cache flags
    move.w CurMap,(%a0)+ // <11> Save CurMap for one deep calls
    cmp.l (%a0),%d7 // Does the cached type match the one requested?
    beq .L_CacheResource_exitCacheResource // If so, exit
    move.l %d7,(%a0)+ // Use existing handle for cache
    addq #4,%a0 // Point to first entry
    bra.w .L_CacheResource_buildCache

//
// Allocate a new handle for the resource cache
//

.L_CacheResource_setupCache:

    move.l #(kInitialEntryCount * kCacheEntrySize) + kCacheHeaderSize,%d0 // Space for header and kInitialEntryCount entries. <29>
    _NewHandleSysClear // Get the handle
    bne .L_CacheResource_exitCacheResource
    move.l %a0,%a3 // Remember the handle
    move.l ExpandMem,%a0
    move.l %a3,emResourceCache(%a0) // Save it in expand mem
    move.l (%a3),%a0
    move.b ResOneDeep,%d0
    andi.w #kCacheIsOneDeepMask,%d0
    move.b ROMMapInsert,%d1 // <34> Remember ROMMapInsert in flags
    andi.w #kCacheHasROMResourcesMask,%d1 // <34>
    or.w %d1,%d0 // <34> Merge the two attributes
    or.w %d3,%d0 // <35> Merge in override setting
    move.w %d0,(%a0)+ // Save deepness in cache flags
    move.w CurMap,(%a0)+ // <11> Save CurMap for one deep calls
    move.l %d7,(%a0)+ // Save the resource type to cache
    addq #4,%a0 // Point to first index
    moveq #kInitialEntryCount,%d6 // Number of entries left before more room is needed

//
// A0 now points to the address to begin saving resource information, and D6 contains
// a zero based count of how many cache entries are still available before the handle
// needs to be grown. A3 contains the cache handle.
//
// Get current map to start walking the resource chain‚Ä¶

.L_CacheResource_buildCache:

    movem.l %a2-%a4/%d1-%d2/%d6,-(%sp) // save registers trashed by GetCurMap
    bsr GetTopMap // get the current maphndl in a4
    move.l %a4,%d0 // keep it in d0
    movem.l (%sp)+,%a2-%a4/%d1-%d2/%d6 // save registers trashed by GetCurMap
    moveq #0,%d5 // Count of entries placed in the cache so far

.L_CacheResource_buildLoop:

    move.l %d0,%a4
    move.l (%a4),%a1 // Dereference map handle
    add.w mTypes(%a1),%a1 // Get to type list
    move.l %a1,%a2 // Copy top of block
    move.w (%a2)+,%d4 // Get number of types in this map
    bmi .L_CacheResource_cacheNextMap // If no resources in this map, go on to next map <SM25> CSS

//
// Find the type in D7 in the resource map.
//

.L_CacheResource_typeSearch:

    cmp.l TType(%a2),%d7 // Is this the right type?
    beq.w .L_CacheResource_foundTypeEntry // Yes, go on
    addq #TSize,%a2 // Otherwise, check next type
    dbra %d4,.L_CacheResource_typeSearch
    bra .L_CacheResource_cacheNextMap // If we fall through, no resources of this type in this map <SM25> CSS

//
// Got the type.  Now determine whether or not to cache each one.
//

.L_CacheResource_foundTypeEntry:

    move.w tCount(%a2),%d4 // Number of resources of this type
    moveq #0,%d2
    move.w tOffset(%a2),%d2 // Remember offset for cache
    add.l %d2,%a1 // Get to reference list from type list
    move.l %a1,%a2 // Keep reference pointer in A2
    move.l ExpandMem,%a1 // <36> If emScanOverrideMaps is false, then treat override maps
    tst.b emScanOverrideMaps(%a1) // <36> as normal resource maps, and always include the overridden resources for counting and indexing
    beq.w .L_CacheResource_addToCache // <36>
    move.l (%a4),%a1 // Point to the resource map
    btst #dontCountOrIndexDuplicatesBit,mInMemoryAttr(%a1) // Is our magic bit set?
    sne %d3 // Use D3 to flag whether this bit was set
.L_CacheResource_addToCacheLoop:

    tst.b %d3 // Check to see if this entry is a duplicate?
    beq.w .L_CacheResource_addToCache // If not, add the resource blindly

//
// dontCountOrIndexDuplicatesBit is set.  Determine whether this entry matches the ID
// of a previous resource in the cache.
//

    bsr IsDuplicate // See if this ID was already used
    bne.w .L_CacheResource_dontAddToCache // If it is, don‚Äôt add it to the cache.

//
// Add this resource to the cache.  Remember the file reference number of the map it
// came from, and its ID.
//

.L_CacheResource_addToCache:

    subq #1,%d6 // Adding another entry
    bge.w .L_CacheResource_doAdd // If there‚Äôs still space left in the cache, add the entry

//
// Out of space in the cache.  Double it‚Äôs size.
//

    move.l (%a3),%a1
    sub.l %a1,%a0 // Compute current byte offset into cache
    exg %a3,%a0 // Get handle
    _gethandlesize
    sub.l #kCacheHeaderSize,%d0 // Don‚Äôt count header info for size calculations
    move.l %d0,%d6 // Need to recalc number of available entries in a sec
    asl.l #1,%d0 // Double the size of the cache
    add.l #kCacheHeaderSize,%d0 // And some space for the header
    _sethandlesize // Set the new size
    bne.w .L_CacheResource_exitCacheResource
    asr.l #3,%d6 // <31> Divide by 8 to get count from size
    subq #1,%d6 // One entry will be used up right away.
    move.l (%a0),%a1 // Get pointer in case it moved
    add.l %a1,%a3 // Add pointer to offset that‚Äôs saved in A3
    exg %a3,%a0 // And swap the handle and the pointer
.L_CacheResource_doAdd:

    addq #1,%d5 // Increment the count
    move.l %a4,(%a0)+ // <31> Remember the map handle
    move.w %d2,(%a0)+ // Remember offset to resource
    move.w rID(%a2),(%a0)+ // <29> Remember ID of this resource
.L_CacheResource_dontAddToCache:

    add.w #RESize,%a2 // Point to next entry
    add.w #RESize,%d2 // Update offset to next resource
    dbra %d4,.L_CacheResource_addToCacheLoop
.L_CacheResource_cacheNextMap:

    tst.b ResOneDeep // if one deep set, don't get next map           <26jun85> BBM
    beq.w .L_CacheResource_notOneDeepMap // ... else do normal stuff

    tst.w doOverrides(%a6) // <41> Is overriding on?
    beq.w .L_CacheResource_endOfChain // <41> No.  don't check the 2 deep bit.


// The requested resource wasn‚Äôt found in the map in A4.  Check if the twoDeepBit is set on this
// map, and if it is, search the next map down.

    move.l (%a4),%a1 // Try the next map, but only if this map has twoDeep set.
    btst #twoDeepBit,mInMemoryAttr(%a1) // Is the twoDeepBit set?
    beq.w .L_CacheResource_endOfChain // ... yes ignore oneDeep, so don't zero handle

.L_CacheResource_notOneDeepMap:

    move.l (%a4),%a1
    move.l mNext(%a1),%d0 // Go until the end of the resource chain
    bne.w .L_CacheResource_buildLoop
.L_CacheResource_endOfChain:

    move.l (%a3),%a0
    move.l %d5,kResourceCount(%a0) // Stash the count
.L_CacheResource_exitCacheResource:

    movem.l (%sp)+,%d2-%d7/%a1-%a4 // Restore registers
    rts // Return

//_______________________________________________________________________________
// IsDuplicate determines whether the resource ID pointed to be A2 is already used
// by another resource entry in the cache pointed to by A3.

IsDuplicate:


    moveq #1,%d0 // Assume it‚Äôs a duplicate
    move.w rID(%a2),%d1 // Keep ID in a register
    movem.l %a0/%d2,-(%sp) // Save A0 & D2
    move.w %d5,%d2 // Make a copy of the current count
    move.l (%a3),%a0
    add.w #kCacheHeaderSize,%a0 // Point to cached resource info
    bra.w .L_IsDuplicate_loopEnd // Let loop fall through if the cache is empty
.L_IsDuplicate_findDuplicates:

    cmp.w kResourceIDEntry(%a0),%d1 // Does the ID match?
    bne.w .L_IsDuplicate_notDuplicate // No.  Try the next entry

// <31> To make the case where a resource is in the System, an application, but not a
//		Gibbly work, check to see if the resource map of the cached entry is an override
//		map for the resource map of the resource we‚Äôre thinking about putting in the
//		cache.  The handle to the second resource map is in A4, and the handle to the
//		first resource map is in the cache.  Call the new routine, DoesCachedMapOverrideCurrentMap,
//		to make this determination.  We might want to make this a selector off of
//		_ResourceDispatch one day if this type of thing needs to be determined a lot.
//
// <47> DoesCachedMapOverrideCurrentMap used to scan the resource chain for duplicates
//		until it finds a map that doesn‚Äôt have any override attributes set.  The theory
//		was that a string of resource maps with override attributes set are associated
//		with each other, like Gibblies and the System, or the System and font files.
//		This test was too generic, and fails in the case where an application has an
//		override map, and the map below the application is a Gibbly or the System.
//		In this case, resources in the System and the application override that have
//		the same type and ID are considered duplicates of each other, which isn‚Äôt right.
//		Now, just look at the two deep bit on resource maps to determine if one is
//		associated with another.
//

    subq #2,%sp // <31>
    move.l %a4,-(%sp) // <31> Resource map handle of current resource
    move.l kMapHandleEntry(%a0),-(%sp) // <31> Resource map handle of resource in cache
    move.w #kTwoDeepMask,-(%sp) // <45> <47> Scan until a map without the two deep attribute is found
    bsr DoesCachedMapOverrideCurrentMap // <31> Go find out if this is true
    tst.b (%sp)+ // <31> Check the result
    bne.w .L_IsDuplicate_duplicateID // <31> The cache entry comes from a resource map which overrides the current resource map, so this resource should be considered a duplicate.
.L_IsDuplicate_notDuplicate:

    addq #kCacheEntrySize,%a0 // Otherwise, try next cache entry
.L_IsDuplicate_loopEnd:

    dbra %d2,.L_IsDuplicate_findDuplicates
.L_IsDuplicate_idNotInCache:

    moveq #0,%d0 // End of cache, and no duplicates
.L_IsDuplicate_duplicateID:

    movem.l (%sp)+,%a0/%d2
    tst.w %d0 // Set up the condition codes
    rts // And return

//_______________________________________________________________________________
// <31> DoesCachedMapOverrideCurrentMap.  Given two resource map handles, this routine
//		determines if one is an override map of the other.  This is done by traversing
//		the resource chain from the second resource map until a map is found that does
//		not have the overrideNextMap bit set.  If any of the resource maps that are
//		visited during the traversal match the first resource map parameter, this routine
//		returns that the current map is overriden by the cached map.
//
// <45>	Added a parameter that specifies the bit combination to test in the mInAttr field
//		of the resource map.  Doing this allows this routine to be used by CacheResource
//		and RemoveResourceOverride, which require checking different bits.
//

DoesCachedMapOverrideCurrentMap:

// RECORD DoesCachedMapOverrideStackFrame {A6Link},Decr
    .space (1)*2 // Space for result
// .equ paramBegin, *
    .space (1)*4 // Handle to current resource map
    .space (1)*4 // Handle to resource map in cache
    .space (1)*2 // <45> Bits in mInAttr to test
// .equ paramSize, paramBegin - *
    .space (1)*4
    .space (1)*4
// ENDR

// WITH DoesCachedMapOverrideStackFrame
    link %a6,#0
    movem.l %d1/%a0/%a1,-(%sp) // <46>
    move.w #-1,result(%a6) // Assume cachedMap does override currentMap
    move.l currentMap(%a6),%a0
    move.l cachedMap(%a6),%a1
    move.w bitsToTest(%a6),%d1 // <45>
.L_A6Link_compareMap:

    cmp.l %a0,%a1 // Do the maps match?
    beq.w .L_A6Link_gotMatch // Yes.  We‚Äôve got an override case

// OK, I lied in all the comments up to now.  Testing just the overrideNextMap bit isn‚Äôt enough,
// because font resources in the Fonts folder would get counted more than once.  To get
// around this, if ANY override bit is set, look at the next map down.

    move.l (%a1),%a1
    move.b mInMemoryAttr(%a1),%d0 // Get override bits
    and.b %d1,%d0 // Mask off everything but the bits of interest
    beq.w .L_A6Link_noMatch // No it‚Äôs not, and we should stop looking now, too.
    move.l mNext(%a1),%d0 // It‚Äôs an override map.  Get the next map, if there is one.
    beq.w .L_A6Link_noMatch // No more maps.
    move.l %d0,%a1
    bra.w .L_A6Link_compareMap

// We got to the end of the resource chain or a non-override map without hitting currentMap.
// Therefore, this resource should not be considered a duplicate of the current cache entry.

.L_A6Link_noMatch:

    clr.w result(%a6)
.L_A6Link_gotMatch:

    movem.l (%sp)+,%d1/%a0/%a1 // <46> Restore used registers
    unlk %a6
    move.l (%sp)+,%d0 // I saved A0 for a reason, so I can‚Äôt use it here‚Ä¶
    add.w #paramSize,%sp // Pop off the parameters
    move.l %d0,-(%sp)
    rts

//_______________________________________________________________________________
//
// Routine:		CheckAndFlushFontCache (utility)
//
// Arguments:	A4		(input.L)		map handle
//
// Called By:	OpenResFile, HOpenResFile, OpenRFPerm, CloseResFile
//
// Calls:		TypeScan
//
// Function: 	If the map in A4 has a 'FONT' or 'FOND' resource we invalidate
//				the font caches.
//_______________________________________________________________________________

CheckAndFlushFontCache:


    movem.l %a0-%a4/%d0-%d5,-(%sp)
    move.l #0x464F4E54,%d3
    bsr TypeScan
    bpl.w .L_CheckAndFlushFontCache_doFlush

    move.l #0x464F4E44,%d3
    bsr TypeScan
    bmi.w .L_CheckAndFlushFontCache_dontFlush

.L_CheckAndFlushFontCache_doFlush:

    move.l MinusOne,LastSPExtra // invalidate font cache
    move.l ExpandMem,%a1
    move.l emSplineKey(%a1),%a1 // Get handle to TrueType globals
    move.l (%a1),%a1 // Point to it.
    lea fondCache(%a1),%a1 // Point to candidate cache
    move.l (%a1),%a0
    _disposehandle // Get rid of this cached entry
    clr.l (%a1)+
    move.l (%a1),%a0 // Nail the next one too
    _disposehandle
    clr.l (%a1)
    clr.l LastFOND // nuke this handle, just in case it was one of the FONDs in this file <52>
.L_CheckAndFlushFontCache_dontFlush:

    bsr FlushResourceCache
    movem.l (%sp)+,%a0-%a4/%d0-%d5
    rts

//_______________________________________________________________________________
//
// Routine:		FlushChangedFONDs
//
// Arguments:	a2 - Handle Entry for the resource we are checking
//
// Called By:	ChangedResource, AddResource, RmveResource
// Calls:		_DisposeHandle
//
// Function: 	invalidate the 'FOND' candidate list if a 'FOND' resource in the
//				candidate cache is being changed
//
// Trashes:		nothing
//
// Written by:	Dean Yu; rolled in by Scott Boyd and Fred Monroe
//_______________________________________________________________________________



FlushChangedFONDs:


    movem.l %a0-%a1/%d0-%d1,-(%sp)

    cmp.l #0x464F4E44,TType(%a2) // Check are we messing w/ a 'FOND' Resource
    bne.w .L_FlushChangedFONDs_notFONDResource // It‚Äôs not a FOND

//
// It‚Äôs a 'FOND'.  If the candidate lists have this 'FOND' cached,
// invalidate it.
//

    move.l ExpandMem,%a0
    move.l emSplineKey(%a0),%a0 // Get handle to TrueType globals
    move.l (%a0),%a0 // Point to it.
    lea fondCache(%a0),%a0 // Point to candidate cache
    move.l (%a0),%a1 // Get Candidate in A1
    bsr CheckCandidate // See if this candidate matches the 'FOND' ID that has changed
    tst.w %d1 // Font family number was left in D0 if handle was not disposed
    bne.w .L_FlushChangedFONDs_notFlushed
    clr.l (%a0) // Clear out candidate entry
.L_FlushChangedFONDs_notFlushed:

    move.l 4(%a0),%a1 // Check the next one, too
    bsr CheckCandidate
    tst.w %d1 // Font family number was left in D0 if handle was not disposed
    bne.w .L_FlushChangedFONDs_notFONDResource
    clr.l 4(%a0)
.L_FlushChangedFONDs_notFONDResource:

    movem.l (%sp)+,%a0-%a1/%d0-%d1
    rts
//
// Compare the ID of the cached FOND information.  If the ID matches the resource that changed,
// invalidate the candidate entry.
//
//	Arguments	A2	(input.L)	resource handle
//				A1	(input.L)	Candidate to check (possibly nil handle)
//				d1	(output.L)	result (Z if disposed)
//
//	Trashes:	d0-d1, A1
//

CheckCandidate:

    moveq #-1,%d1 // Assume handle is not disposed
    move.l %a1,%d0 // Get the handle
    beq.w .L_CheckCandidate_notSameFamily // Bail out if handle is NIL
    move.l (%a1),%d0 // Get the pointer
    beq.w .L_CheckCandidate_notSameFamily // Bail again if needed
    move.l %d0,%a1
    moveq #-1,%d1 // Assume handle is not disposed
    move.w (%a1),%d0 // Fetch the font family number
    cmp.w RID(%a2),%d0 // Compare it against the resource ID of the changed 'FOND'
    bne.w .L_CheckCandidate_notSameFamily // Don‚Äôt invalidate if not the right ID
    _disposehandle // If ID matches, dispose the handle
    moveq #0,%d1 // Mark as handle was disposed
.L_CheckCandidate_notSameFamily:

    rts


//_______________________________________________________________________________
//
// Routine:		UpdateOverriddenChangedBits (utility)
//
// Arguments:	A4		(input.L)		map handle
//
// Called By:	Anywhere a mapChanged bit is set‚Ä¶
//
// Calls:
//
// Function: 	If the map in A4 overrides another file than we set the changed bit
//				in that overridden file.
//_______________________________________________________________________________

UpdateOverriddenChangedBits:


    movem.l %a0-%a2/%a4/%d0-%d1,-(%sp)

    move.l (%a4),%a0
    move.b mInMemoryAttr(%a0),%d0 // Get the override bits
    andi.b #kAllOverrideAttributesMask,%d0 // This and masks off everything but override bits.  If any bits
    beq.w .L_UpdateOverriddenChangedBits_exit // are left in D0, then this map has some override attributes.

// See if this is an override map

    btst #overrideNextMapBit,%d0 // We‚Äôve still got our override attributes in D0
    beq.w .L_UpdateOverriddenChangedBits_notAnOverrideMap // This is an override map, but it might be a font file

// This is an override map.  Find the map it overrides.

    subq #4,%sp
    move.l %a4,-(%sp)
    bsr FindOverriddenMap // trashes a0-a1/d0
    move.l (%sp)+,%a4 // Save the handle to the overriden map
    bra.w .L_UpdateOverriddenChangedBits_setMapChangedOnOverrideMap // Go set the bit

.L_UpdateOverriddenChangedBits_notAnOverrideMap:

    subq #2,%sp
    move.l %a4,-(%sp)
    _isthisasystemresourcemap // See if this is a font file
    tst.b (%sp)+ // Well is it?
    beq.w .L_UpdateOverriddenChangedBits_exit // If it‚Äôs not, then we don‚Äôt know why this map has override bits on it.  Just call AddResource, and don‚Äôt set the mapChanged bit anywhere else.
    move.l SysMapHndl,%a4 // If it is, set the mapChanged bit of the system map

.L_UpdateOverriddenChangedBits_setMapChangedOnOverrideMap:

    move.l (%a4),%a0
    bset #mapChanged,mAttr(%a0) // Mark the overridden map as changed
.L_UpdateOverriddenChangedBits_exit:

    movem.l (%sp)+,%a0-%a2/%a4/%d0-%d1
    rts
//_______________________________________________________________________________
// FindOverriddenMap
//
// Given a handle to a resource override map, find the map it overrides.  This routine
// assumes that the map passed in is an override map.
//
// trashes a0-a1/d0

FindOverriddenMap:

    move.l (%sp)+,%a0
    move.l (%sp)+,%d0 // Get resource map handle
    move.l %a0,-(%sp)

.L_FindOverriddenMap_findOverriddenMapLoop:

    move.l %d0,%a1
    move.l (%a1),%a0
    btst #overrideNextMapBit,mInMemoryAttr(%a0) // This bit says whether or not this is an override map
    beq.w .L_FindOverriddenMap_foundOverriddenMap // If the bit isn‚Äôt set, then we‚Äôve got the overridden map
    move.l mNext(%a0),%d0 // Otherwise, look at the next map down
    bne.w .L_FindOverriddenMap_findOverriddenMapLoop
    move.l %d0,%a1 // If we didn‚Äôt find the overridden map before reaching the end of the chain, return nil.
.L_FindOverriddenMap_foundOverriddenMap:

    move.l %a1,4(%sp) // Return result on the stack
    rts

// <SM11> CSS Add the following routine for rsrczoneinit patch
//_______________________________________________________________________________
// FlushResourceCache disposes the resource cache handle to force the cache to
// be rebuilt next time _CountResources or _GetIndResource is called.  The cache
// will be flushed whenever the offset to a resource stored in the cache may become
// invalid (when resources are added or removed from the map or when the map is updated or
// written to disk) and when files are opened or closed which may cause the count to
// become invalid.
//
// should preserve a0‚Ä¶

FlushResourceCache:

    movem.l %d0/%a0,-(%sp)
    move.l ExpandMem,%a0
    clr.w emLastMapOverridden(%a0) // And force an override synchronization next time <SM51> CSS
    move.l emResourceCache(%a0),%d0 // Get resource cache handle
    beq.w .L_FlushResourceCache_noExpandMem // Doesn‚Äôt exist
    clr.l emResourceCache(%a0) // Zero it out
    move.l %d0,%a0
    _disposehandle // Get rid of the cache
.L_FlushResourceCache_noExpandMem:

    movem.l (%sp)+,%d0/%a0
    rts

// end
//=== End Of File ========================================================================




/* RM trap installation table: Toolbox table offsets + handler addresses */
rm_trap_table:
    .word 0x0034
    .long 0x40848F3A
    .word 0x0038
    .long 0x40848F68
    .word 0x003C
    .long 0x40849050
    .word 0x0040
    .long 0x4084907C
    .word 0x006C
    .long 0x4084882E
    .word 0x0070
    .long 0x40848FFC
    .word 0x007C
    .long 0x408490C8
    .word 0x0080
    .long 0x4084919C
    .word 0x0084
    .long 0x4084A3A0
    .word 0x0648
    .long 0x40849DCA
    .word 0x064C
    .long 0x40849EC0
    .word 0x0650
    .long 0x40849EF6
    .word 0x0654
    .long 0x4084813E
    .word 0x0658
    .long 0x40848722
    .word 0x065C
    .long 0x4084890E
    .word 0x0660
    .long 0x408489B8
    .word 0x0664
    .long 0x40848AC2
    .word 0x0668
    .long 0x40848E6E
    .word 0x066C
    .long 0x40849EEA
    .word 0x0670
    .long 0x40848F3E
    .word 0x0674
    .long 0x40848F6C
    .word 0x0678
    .long 0x40849000
    .word 0x067C
    .long 0x40849054
    .word 0x0680
    .long 0x408490D0
    .word 0x0684
    .long 0x408491A0
    .word 0x0688
    .long 0x40849D2E
    .word 0x068C
    .long 0x40849D94
    .word 0x0690
    .long 0x40849E86
    .word 0x0694
    .long 0x4084A37A
    .word 0x0698
    .long 0x40849F28
    .word 0x069C
    .long 0x40849F40
    .word 0x06A0
    .long 0x40849F7E
    .word 0x06A4
    .long 0x40849FD2
    .word 0x06A8
    .long 0x40849DF2
    .word 0x06AC
    .long 0x4084A02C
    .word 0x06B0
    .long 0x40849F0A
    .word 0x06B4
    .long 0x4084A146
    .word 0x06B8
    .long 0x40849F1C
    .word 0x06BC
    .long 0x40849F00
    .word 0x06C0
    .long 0x40849E3E
    .word 0x06C4
    .long 0x40848892
    .word 0x0704
    .long 0x40849080
    .word 0x0710
    .long 0x4084896A
    .word 0x07D8
    .long 0x408489E8
    .word 0x07DC
    .long 0x408489F6
    .word 0xFFFF  /* end marker */

/* Debug strings for $A0FE print trap */
.Ldbg_redomap:   .asciz "RM:ReDoMap"
.Ldbg_aftermap:  .asciz "RM:AfterReDoMap"
.Ldbg_exit:      .asciz "RM:InitResExit"
.Ldbg_loopdone:  .asciz "RM:LoopDone n="
.Ldbg_addrom:    .asciz "RM:AddRom"
.Ldbg_ar1:       .asciz "RM:AR hndl="
.Ldbg_ar2:       .asciz "RM:AR GetCurMap err="
.Ldbg_ar3:       .asciz "RM:AR AddNewRef type="
.Ldbg_ar4:       .asciz "RM:AR AfterNewRef ref="
.Ldbg_scrn:      .asciz "RM:ScrnBase="
.Ldbg_rts:       .asciz "RM:InitRMgr RTS to="
.Ldbg_pre_ir:    .asciz "RM:pre_IR SP="
.Ldbg_post_ir:   .asciz "RM:post_IR SP="
.Ldbg_anr0:      .asciz "RM:ANR entry A3="
.Ldbg_anrTS:     .asciz "RM:ANR preTS"
.Ldbg_anrTSr:    .asciz "RM:ANR TS result="
.Ldbg_anr1:      .asciz "RM:ANR type exists"
.Ldbg_anr2:      .asciz "RM:ANR MoveNames done"
.Ldbg_anr3:      .asciz "RM:ANR TypeScan2 A2="
    .align 2

/* _ResourceDispatch ($A822) stub — SE ROM handler crashes on System 7 selectors.
 * Handle the selectors that vCheckLoad and vRMgrStdEntry call.
 * D0 = selector: -1=IsThisASystemResourceMap, 6=GetOverrideMap, etc.
 * Note: called via emulator intercept which pushes retPC before entry. */
ResourceDispatchStub:
    cmpi.b #-1,%d0
    beq.w .Lrd_IsSystem
    cmpi.b #6,%d0
    beq.w .Lrd_GetOverride
    rts                         /* unknown selector: no-op */

.Lrd_IsSystem:
    /* _IsThisASystemResourceMap(mapHndl): BOOLEAN
     * Stack: [retPC:4][mapHndl:4][result:2]
     * Compare mapHndl to SysMapHndl */
    move.l (%sp)+,%a0           /* pop return PC */
    move.l (%sp)+,%a1           /* pop mapHndl parameter */
    cmp.l SysMapHndl,%a1        /* is it the system map? */
    seq (%sp)                   /* result = $FF if equal, $00 if not */
    jmp (%a0)                   /* return */

.Lrd_GetOverride:
    /* _GetOverrideMap(mapHndl): Handle
     * Stack: [retPC:4][mapHndl:4][result:4]
     * Pop mapHndl, set result=nil */
    move.l (%sp)+,%a0           /* pop return PC */
    addq.l #4,%sp               /* pop mapHndl parameter */
    clr.l (%sp)                 /* result = nil */
    jmp (%a0)                   /* return */

/* Stub functions for unneeded external references */
PreLoad:
PreLoad2:
SmgrInitialize:
DoDonnDecompress:
DiskCachePurgeProcedure:
UpdateCompact:
ZNextRes:
    rts

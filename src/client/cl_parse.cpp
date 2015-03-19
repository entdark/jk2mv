// cl_parse.c  -- parse a message received from the server

#include "client.h"
#include "../qcommon/strip.h"
#include "../ghoul2/G2_local.h"
#ifdef _DONETPROFILE_
#include "../qcommon/INetProfile.h"
#endif

char *svc_strings[256] = {
	"svc_bad",

	"svc_nop",
	"svc_gamestate",
	"svc_configstring",
	"svc_baseline",
	"svc_serverCommand",
	"svc_download",
	"svc_snapshot",
	"svc_mapchange",
};

void SHOWNET( msg_t *msg, char *s) {
	if ( cl_shownet->integer >= 2) {
		Com_Printf ("%3i:%s\n", msg->readcount-1, s);
	}
}

void CL_SP_Print(const word ID, byte *Data); //, char* color)

/*
=========================================================================

MESSAGE PARSING

=========================================================================
*/

/*
==================
CL_DeltaEntity

Parses deltas from the given base and adds the resulting entity
to the current frame
==================
*/
void CL_DeltaEntity (msg_t *msg, clSnapshot_t *frame, int newnum, entityState_t *old,
					 qboolean unchanged) {
	entityState_t	*state;

	// save the parsed entity state into the big circular buffer so
	// it can be used as the source for a later delta
	state = &cl.parseEntities[cl.parseEntitiesNum & (MAX_PARSE_ENTITIES-1)];

	if ( unchanged )
	{
		*state = *old;
	}
	else
	{
		MSG_ReadDeltaEntity( msg, old, state, newnum );
	}

	if ( state->number == (MAX_GENTITIES-1) ) {
		return;		// entity was delta removed
	}
	cl.parseEntitiesNum++;
	frame->numEntities++;
}

/*
==================
CL_ParsePacketEntities

==================
*/
void CL_ParsePacketEntities( msg_t *msg, clSnapshot_t *oldframe, clSnapshot_t *newframe) {
	int			newnum;
	entityState_t	*oldstate;
	int			oldindex, oldnum;

	newframe->parseEntitiesNum = cl.parseEntitiesNum;
	newframe->numEntities = 0;

	// delta from the entities present in oldframe
	oldindex = 0;
	oldstate = NULL;
	if (!oldframe) {
		oldnum = 99999;
	} else {
		if ( oldindex >= oldframe->numEntities ) {
			oldnum = 99999;
		} else {
			oldstate = &cl.parseEntities[
				(oldframe->parseEntitiesNum + oldindex) & (MAX_PARSE_ENTITIES-1)];
			oldnum = oldstate->number;
		}
	}

	while ( 1 ) {
		// read the entity index number
		newnum = MSG_ReadBits( msg, GENTITYNUM_BITS );

		if ( newnum == (MAX_GENTITIES-1) ) {
			break;
		}

		if ( msg->readcount > msg->cursize ) {
			Com_Error (ERR_DROP,"CL_ParsePacketEntities: end of message");
		}

		while ( oldnum < newnum ) {
			// one or more entities from the old packet are unchanged
			if ( cl_shownet->integer == 3 ) {
				Com_Printf ("%3i:  unchanged: %i\n", msg->readcount, oldnum);
			}
			CL_DeltaEntity( msg, newframe, oldnum, oldstate, qtrue );

			oldindex++;

			if ( oldindex >= oldframe->numEntities ) {
				oldnum = 99999;
			} else {
				oldstate = &cl.parseEntities[
					(oldframe->parseEntitiesNum + oldindex) & (MAX_PARSE_ENTITIES-1)];
				oldnum = oldstate->number;
			}
		}
		if (oldnum == newnum) {
			// delta from previous state
			if ( cl_shownet->integer == 3 ) {
				Com_Printf ("%3i:  delta: %i\n", msg->readcount, newnum);
			}
			CL_DeltaEntity( msg, newframe, newnum, oldstate, qfalse );

			oldindex++;

			if ( oldindex >= oldframe->numEntities ) {
				oldnum = 99999;
			} else {
				oldstate = &cl.parseEntities[
					(oldframe->parseEntitiesNum + oldindex) & (MAX_PARSE_ENTITIES-1)];
				oldnum = oldstate->number;
			}
			continue;
		}

		if ( oldnum > newnum ) {
			// delta from baseline
			if ( cl_shownet->integer == 3 ) {
				Com_Printf ("%3i:  baseline: %i\n", msg->readcount, newnum);
			}
			CL_DeltaEntity( msg, newframe, newnum, &cl.entityBaselines[newnum], qfalse );
			continue;
		}

	}

	// any remaining entities in the old frame are copied over
	while ( oldnum != 99999 ) {
		// one or more entities from the old packet are unchanged
		if ( cl_shownet->integer == 3 ) {
			Com_Printf ("%3i:  unchanged: %i\n", msg->readcount, oldnum);
		}
		CL_DeltaEntity( msg, newframe, oldnum, oldstate, qtrue );

		oldindex++;

		if ( oldindex >= oldframe->numEntities ) {
			oldnum = 99999;
		} else {
			oldstate = &cl.parseEntities[
				(oldframe->parseEntitiesNum + oldindex) & (MAX_PARSE_ENTITIES-1)];
			oldnum = oldstate->number;
		}
	}
}


/*
================
CL_ParseSnapshot

If the snapshot is parsed properly, it will be copied to
cl.snap and saved in cl.snapshots[].  If the snapshot is invalid
for any reason, no changes to the state will be made at all.
================
*/
void CL_ParseSnapshot( msg_t *msg ) {
	int			len;
	clSnapshot_t	*old;
	clSnapshot_t	newSnap;
	int			deltaNum;
	int			oldMessageNum;
	int			i, packetNum;

	// get the reliable sequence acknowledge number
	// NOTE: now sent with all server to client messages
	//clc.reliableAcknowledge = MSG_ReadLong( msg );

	// read in the new snapshot to a temporary buffer
	// we will only copy to cl.snap if it is valid
	Com_Memset (&newSnap, 0, sizeof(newSnap));

	// we will have read any new server commands in this
	// message before we got to svc_snapshot
	newSnap.serverCommandNum = clc.serverCommandSequence;

	newSnap.serverTime = MSG_ReadLong( msg );

	newSnap.messageNum = clc.serverMessageSequence;

	deltaNum = MSG_ReadByte( msg );
	if ( !deltaNum ) {
		newSnap.deltaNum = -1;
	} else {
		newSnap.deltaNum = newSnap.messageNum - deltaNum;
	}
	newSnap.snapFlags = MSG_ReadByte( msg );

	// If the frame is delta compressed from data that we
	// no longer have available, we must suck up the rest of
	// the frame, but not use it, then ask for a non-compressed
	// message
	if ( newSnap.deltaNum <= 0 ) {
		newSnap.valid = qtrue;		// uncompressed frame
		old = NULL;
		clc.demowaiting = qfalse;	// we can start recording now
	} else {
		old = &cl.snapshots[newSnap.deltaNum & PACKET_MASK];
		if ( !old->valid ) {
			// should never happen
			Com_Printf ("Delta from invalid frame (not supposed to happen!).\n");
		} else if ( old->messageNum != newSnap.deltaNum ) {
			// The frame that the server did the delta from
			// is too old, so we can't reconstruct it properly.
			Com_Printf ("Delta frame too old.\n");
		} else if ( cl.parseEntitiesNum - old->parseEntitiesNum > MAX_PARSE_ENTITIES-128 ) {
			Com_DPrintf ("Delta parseEntitiesNum too old.\n");
		} else {
			newSnap.valid = qtrue;	// valid delta parse
		}
	}

	// read areamask
	len = MSG_ReadByte( msg );
	MSG_ReadData( msg, &newSnap.areamask, len);

	// read playerinfo
	SHOWNET( msg, "playerstate" );
	if ( old ) {
		MSG_ReadDeltaPlayerstate( msg, &old->ps, &newSnap.ps );
	} else {
		MSG_ReadDeltaPlayerstate( msg, NULL, &newSnap.ps );
	}

	// read packet entities
	SHOWNET( msg, "packet entities" );
	CL_ParsePacketEntities( msg, old, &newSnap );

	// if not valid, dump the entire thing now that it has
	// been properly read
	if ( !newSnap.valid ) {
		return;
	}

	// clear the valid flags of any snapshots between the last
	// received and this one, so if there was a dropped packet
	// it won't look like something valid to delta from next
	// time we wrap around in the buffer
	oldMessageNum = cl.snap.messageNum + 1;

	if ( newSnap.messageNum - oldMessageNum >= PACKET_BACKUP ) {
		oldMessageNum = newSnap.messageNum - ( PACKET_BACKUP - 1 );
	}
	for ( ; oldMessageNum < newSnap.messageNum ; oldMessageNum++ ) {
		cl.snapshots[oldMessageNum & PACKET_MASK].valid = qfalse;
	}

	// copy to the current good spot
	cl.snap = newSnap;
	cl.snap.ping = 999;
	// calculate ping time
	for ( i = 0 ; i < PACKET_BACKUP ; i++ ) {
		packetNum = ( clc.netchan.outgoingSequence - 1 - i ) & PACKET_MASK;
		if ( cl.snap.ps.commandTime >= cl.outPackets[ packetNum ].p_serverTime ) {
			cl.snap.ping = cls.realtime - cl.outPackets[ packetNum ].p_realtime;
			break;
		}
	}
	// save the frame off in the backup array for later delta comparisons
	cl.snapshots[cl.snap.messageNum & PACKET_MASK] = cl.snap;

	if (cl_shownet->integer == 3) {
		Com_Printf( "   snapshot:%i  delta:%i  ping:%i\n", cl.snap.messageNum,
		cl.snap.deltaNum, cl.snap.ping );
	}

	cl.newSnapshots = qtrue;
}


//=====================================================================

int cl_connectedToPureServer;

/*
==================
CL_SystemInfoChanged

The systeminfo configstring has been changed, so parse
new information out of it.  This will happen at every
gamestate, and possibly during gameplay.
==================
*/
void CL_SystemInfoChanged( void ) {
	char			*systemInfo;
	const char		*s, *t;
	char			key[BIG_INFO_KEY];
	char			value[BIG_INFO_VALUE];
	qboolean		gameSet;

	systemInfo = cl.gameState.stringData + cl.gameState.stringOffsets[ CS_SYSTEMINFO ];
	cl.serverId = atoi( Info_ValueForKey( systemInfo, "sv_serverid" ) );

	// don't set any vars when playing a demo
	if ( clc.demoplaying ) {
		return;
	}

	s = Info_ValueForKey( systemInfo, "sv_cheats" );
	if ( atoi(s) == 0 )
	{
		Cvar_SetCheatState();
	}

	// check pure server string
	s = Info_ValueForKey( systemInfo, "sv_paks" );
	t = Info_ValueForKey( systemInfo, "sv_pakNames" );
	FS_PureServerSetLoadedPaks( s, t );

	s = Info_ValueForKey( systemInfo, "sv_referencedPaks" );
	t = Info_ValueForKey( systemInfo, "sv_referencedPakNames" );
	FS_PureServerSetReferencedPaks( s, t );

	gameSet = qfalse;
	// scan through all the variables in the systeminfo and locally set cvars to match
	s = systemInfo;
	while ( s ) {
		Info_NextPair( &s, key, value );
		if ( !key[0] ) {
			break;
		}
		// ehw!
		if ( !Q_stricmp( key, "fs_game" ) ) {
			gameSet = qtrue;
		}

		Cvar_Set( key, value );
	}
	// if game folder should not be set and it is set at the client side
	if ( !gameSet && *Cvar_VariableString("fs_game") ) {
		Cvar_Set( "fs_game", "" );
	}
	cl_connectedToPureServer = Cvar_VariableValue( "sv_pure" );
}

/*
==================
CL_ParseGamestate
==================
*/
void CL_ParseGamestate( msg_t *msg ) {
	int				i;
	entityState_t	*es;
	int				newnum;
	entityState_t	nullstate;
	int				cmd;
	char			*s;

	Con_Close();

	clc.connectPacketCount = 0;

	// wipe local client state
	CL_ClearState();

#ifdef _DONETPROFILE_
	int startBytes,endBytes;
	startBytes=msg->readcount;
#endif

	// a gamestate always marks a server command sequence
	clc.serverCommandSequence = MSG_ReadLong( msg );

	// parse all the configstrings and baselines
	cl.gameState.dataCount = 1;	// leave a 0 at the beginning for uninitialized configstrings
	while ( 1 ) {
		cmd = MSG_ReadByte( msg );

		if ( cmd == svc_EOF ) {
			break;
		}

		if ( cmd == svc_configstring ) {
			int		len;

			i = MSG_ReadShort( msg );
			if ( i < 0 || i >= MAX_CONFIGSTRINGS ) {
				Com_Error( ERR_DROP, "configstring > MAX_CONFIGSTRINGS" );
			}
			s = MSG_ReadBigString( msg );
			len = (int)strlen(s);

			if ( len + 1 + cl.gameState.dataCount > MAX_GAMESTATE_CHARS ) {
				Com_Error( ERR_DROP, "MAX_GAMESTATE_CHARS exceeded" );
			}

			// append it to the gameState string buffer
			cl.gameState.stringOffsets[ i ] = cl.gameState.dataCount;
			Com_Memcpy( cl.gameState.stringData + cl.gameState.dataCount, s, len + 1 );
			cl.gameState.dataCount += len + 1;
		} else if ( cmd == svc_baseline ) {
			newnum = MSG_ReadBits( msg, GENTITYNUM_BITS );
			if ( newnum < 0 || newnum >= MAX_GENTITIES ) {
				Com_Error( ERR_DROP, "Baseline number out of range: %i", newnum );
			}
			Com_Memset (&nullstate, 0, sizeof(nullstate));
			es = &cl.entityBaselines[ newnum ];
			MSG_ReadDeltaEntity( msg, &nullstate, es, newnum );
		} else {
			Com_Error( ERR_DROP, "CL_ParseGamestate: bad command byte" );
		}
	}

	clc.clientNum = MSG_ReadLong(msg);
	// read the checksum feed
	clc.checksumFeed = MSG_ReadLong( msg );

#ifdef _DONETPROFILE_
	endBytes=msg->readcount;
//	ClReadProf().AddField("svc_gamestate",endBytes-startBytes);
#endif

	// parse serverId and other cvars
	CL_SystemInfoChanged();

	// reinitialize the filesystem if the game directory has changed
	if( FS_ConditionalRestart( clc.checksumFeed ) ) {
		// don't set to true because we yet have to start downloading
		// enabling this can cause double loading of a map when connecting to
		// a server which has a different game directory set
		//clc.downloadRestart = qtrue;
	}

	// This used to call CL_StartHunkUsers, but now we enter the download state before loading the
	// cgame
	CL_InitDownloads();

	// make sure the game starts
	Cvar_Set( "cl_paused", "0" );
}


//=====================================================================

/*
=====================
CL_ParseDownload

A download message has been received from the server
=====================
*/

mvmutex_t m_dl;
char m_remotepath[MAX_STRING_CHARS];
char m_tmppath[MAX_QPATH];
char m_tmpospath[MAX_OSPATH];
mvhttpstatus_t m_status;
CURLcode m_error;
size_t m_filesize, m_filecount;

// ouned: this is ugly but it syncs everything correctly
// THIS FUNCTION IS NOT CALLED ON THE MAIN THREAD
size_t CL_MV_HTTP_FileWriter_ExtThread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
	size_t written = fwrite(ptr, size, nmemb, stream);
	return written;
}

// http://curl.haxx.se/libcurl/c/CURLOPT_XFERINFOFUNCTION.html
// atleast called one time per second (good for aborting)
int CL_MV_HTTP_Process_ExtThread(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
	MV_LockMutex(m_dl);

	if (m_status == MVHTTP_NOTHING) {
		MV_ReleaseMutex(m_dl);
		return 1;
	}

	if (dltotal && dlnow) {
		m_status = MVHTTP_PROCESS;
	}

	m_filesize = (unsigned long)dltotal;
	m_filecount = (unsigned long)dlnow;

	MV_ReleaseMutex(m_dl);
	return 0;
}

#ifndef INTERNAL_CURL
int CL_MV_HTTP_OldProcess_ExtThread(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow) {
	return CL_MV_HTTP_Process_ExtThread(clientp, (curl_off_t)dltotal, (curl_off_t)dlnow, (curl_off_t)ultotal, (curl_off_t)ulnow);
}
#endif

void *CL_MV_HTTP_FileDownload_ExtThread(void *unused) {
	char m_int_remotepath[MAX_STRING_CHARS];
	char m_int_tmppath[MAX_OSPATH];
	CURL *curl;
	CURLcode code;
	FILE *tmpfile;

	MV_LockMutex(m_dl);

	// ouned: copy over download paths to be sure
	strcpy(m_int_remotepath, m_remotepath);
	strcpy(m_int_tmppath, m_tmpospath);

	curl = curl_easy_init();
	if (!curl) {
		m_status = MVHTTP_FINISHED;
		m_error = CURLE_FAILED_INIT;
		MV_ReleaseMutex(m_dl);
		return NULL;
	}

	tmpfile = fopen(m_int_tmppath, "wb");
	if (!tmpfile) {
		m_status = MVHTTP_FINISHED;
		m_error = CURLE_WRITE_ERROR;
		curl_easy_cleanup(curl);
		MV_ReleaseMutex(m_dl);
		return NULL;
	}

	m_status = MVHTTP_RUNNING;
	MV_ReleaseMutex(m_dl);

	curl_easy_setopt(curl, CURLOPT_URL, m_int_remotepath);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, tmpfile);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CL_MV_HTTP_FileWriter_ExtThread);
#ifndef INTERNAL_CURL
	// ouned: XFERINFOFUNCTION is pretty new...
	curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, CL_MV_HTTP_OldProcess_ExtThread);
#else
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CL_MV_HTTP_Process_ExtThread);
#endif
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8);
#ifdef _DEBUG
	curl_easy_setopt(curl, CURLOPT_MAX_RECV_SPEED_LARGE, (curl_off_t)(2.5 * 1024 * 1024)); // ouned: my eyes aren't fast enough for this
#endif
	code = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	fclose(tmpfile);

	MV_LockMutex(m_dl);

	m_status = MVHTTP_FINISHED;
	m_error = code;

	MV_ReleaseMutex(m_dl);
	return NULL;
}

void CL_MV_HTTP_HTTPControl_MainThread(qboolean abort) {
	MV_LockMutex(m_dl);

	if (m_status == MVHTTP_PROCESS) {
		Cvar_SetValue("cl_downloadSize", (int)m_filesize);
		Cvar_SetValue("cl_downloadCount", (int)m_filecount);
	} else if (m_status == MVHTTP_FINISHED) {
		if (m_error == CURLE_OK) {
			// file is there
			FS_SV_Rename(m_tmppath, Cvar_Get("cl_downloadLocalName", "", 0)->string);
		} else {
			remove(m_tmpospath);
			if (m_error != CURLE_ABORTED_BY_CALLBACK)
				Com_Error(ERR_DROP, "^3JK2MV: HTTP Download of %s failed with error %s\n", Cvar_Get("cl_downloadLocalName", "", 0)->string, curl_easy_strerror(m_error));
			else
				Com_Printf("JK2MV: HTTP Download aborted by user\n");
		}

		m_status = MVHTTP_NOTHING;
		m_error = CURLE_OK;
		m_filesize = 0;
		m_filecount = 0;
		Cvar_Set("cl_downloadName", "");

		if (cls.state > CA_DISCONNECTED)
			CL_NextDownload();
	}

	if (abort) {
		m_status = MVHTTP_NOTHING;
	}

	MV_ReleaseMutex(m_dl);
}

void CL_ParseDownload ( msg_t *msg ) {
	int		size;
	unsigned char data[MAX_MSGLEN];
	int block;

	// read the data
	block = (unsigned short)MSG_ReadShort ( msg );

	if ( !block )
	{
		// block zero is special, contains file size
		clc.downloadSize = MSG_ReadLong ( msg );

		Cvar_SetValue( "cl_downloadSize", clc.downloadSize );

		if (clc.downloadSize < 0)
		{
			Com_Error(ERR_DROP, MSG_ReadString( msg ));
			CL_DownloadsComplete();
			return;
		}
	}

	size = (unsigned short)MSG_ReadShort ( msg );
	if (size > 0)
		MSG_ReadData( msg, data, size );

	if (clc.downloadBlock != block) {
		Com_DPrintf( "CL_ParseDownload: Expected block %d, got %d\n", clc.downloadBlock, block);
		return;
	}

	// open the file if not opened yet
	if (!clc.download)
	{
		if (!*clc.downloadTempName) {
			Com_Printf("Server sending download, but no download was requested\n");
			CL_AddReliableCommand( "stopdl" );
			return;
		}

		clc.download = FS_SV_FOpenFileWrite( clc.downloadTempName );

		if (!clc.download) {
			Com_Printf( "Could not create %s\n", clc.downloadTempName );
			CL_AddReliableCommand( "stopdl" );
			CL_NextDownload();
			return;
		}
	}

	if (size)
		FS_Write( data, size, clc.download );

	CL_AddReliableCommand( va("nextdl %d", clc.downloadBlock) );
	clc.downloadBlock++;

	clc.downloadCount += size;

	// So UI gets access to it
	Cvar_SetValue( "cl_downloadCount", clc.downloadCount );

	if (!size) { // A zero length block means EOF
		if (clc.download) {
			FS_FCloseFile( clc.download );
			clc.download = 0;

			FS_SV_Rename(clc.downloadTempName, clc.downloadName);
		}
		*clc.downloadTempName = *clc.downloadName = 0;
		Cvar_Set( "cl_downloadName", "" );

		// send intentions now
		// We need this because without it, we would hold the last nextdl and then start
		// loading right away.  If we take a while to load, the server is happily trying
		// to send us that last block over and over.
		// Write it twice to help make sure we acknowledge the download
		CL_WritePacket();
		CL_WritePacket();

		// get another file if needed
		CL_NextDownload ();
	}
}

/*
=====================
CL_ParseCommandString

Command strings are just saved off until cgame asks for them
when it transitions a snapshot
=====================
*/
void CL_ParseCommandString( msg_t *msg ) {
	char	*s;
	int		seq;
	int		index;

#ifdef _DONETPROFILE_
	int startBytes,endBytes;
	startBytes=msg->readcount;
#endif
	seq = MSG_ReadLong( msg );
	s = MSG_ReadString( msg );
#ifdef _DONETPROFILE_
	endBytes=msg->readcount;
	ClReadProf().AddField("svc_serverCommand",endBytes-startBytes);
#endif
	// see if we have already executed stored it off
	if ( clc.serverCommandSequence >= seq ) {
		return;
	}
	clc.serverCommandSequence = seq;

	index = seq & (MAX_RELIABLE_COMMANDS-1);
	Q_strncpyz( clc.serverCommands[ index ], s, sizeof( clc.serverCommands[ index ] ) );
}


/*
=====================
CL_ParseServerMessage
=====================
*/
void CL_ParseServerMessage( msg_t *msg ) {
	int			cmd;

	if ( cl_shownet->integer == 1 ) {
		Com_Printf ("%i ",msg->cursize);
	} else if ( cl_shownet->integer >= 2 ) {
		Com_Printf ("------------------\n");
	}

	MSG_Bitstream(msg);

	// get the reliable sequence acknowledge number
	clc.reliableAcknowledge = MSG_ReadLong( msg );
	//
	if ( clc.reliableAcknowledge < clc.reliableSequence - MAX_RELIABLE_COMMANDS ) {
		clc.reliableAcknowledge = clc.reliableSequence;
	}

	//
	// parse the message
	//
	while ( 1 ) {
		if ( msg->readcount > msg->cursize ) {
			Com_Error (ERR_DROP,"CL_ParseServerMessage: read past end of server message");
			break;
		}

		cmd = MSG_ReadByte( msg );

		if ( cmd == svc_EOF) {
			SHOWNET( msg, "END OF MESSAGE" );
			break;
		}

		if ( cl_shownet->integer >= 2 ) {
			if ( !svc_strings[cmd] ) {
				Com_Printf( "%3i:BAD CMD %i\n", msg->readcount-1, cmd );
			} else {
				SHOWNET( msg, svc_strings[cmd] );
			}
		}

	// other commands
		switch ( cmd ) {
		default:
			Com_Error (ERR_DROP,"CL_ParseServerMessage: Illegible server message\n");
			break;
		case svc_nop:
			break;
		case svc_serverCommand:
			CL_ParseCommandString( msg );
			break;
		case svc_gamestate:
			CL_ParseGamestate( msg );
			break;
		case svc_snapshot:
			CL_ParseSnapshot( msg );
			break;
		case svc_download:
			CL_ParseDownload( msg );
			break;
		case svc_mapchange:
			if (cgvm)
			{
				VM_Call( cgvm, CG_MAP_CHANGE );
			}
			break;
		}
	}
}


extern int			scr_center_y;
void SCR_CenterPrint (char *str);//, PalIdx_t colour)

void CL_SP_Print(const word ID, byte *Data) //, char* color)
{
	cStringsSingle	*String;
	unsigned int	Flags;
	char			temp[1024], *Text;

	String = SP_GetString(ID);
	if (String)
	{
		Text = String->GetText();
		if (Data)
		{
			Com_sprintf(temp, sizeof(temp), Text, Data);
			Text = temp;
		}

		Flags = String->GetFlags();

		// RWL - commented out.
		/*
		if (Flags & SP_FLAG_CREDIT)
		{
			SCR_FadePic(Text);
		}
		// RWL - commented out.
		
		else if (Flags & SP_FLAG_CAPTIONED)
		{
			if (( cl_subtitles->value )|| (Flags & SP_FLAG_ALWAYS_PRINT))
			{
				scr_center_y = 1;
				SCR_CenterPrint (Text);
			}
		}
		else if (Flags & SP_FLAG_CENTERED)
		*/
		if (Flags & SP_FLAG1)
		{
			scr_center_y = 0;
			SCR_CenterPrint (Text);
		}
		// RWL - commented out.
		/*
		else if (Flags & SP_FLAG_TYPEAMATIC)
		{
			SCR_CinematicString(0,104,5,Text);
		}
		else if (Flags & SP_FLAG_LAYOUT)
		{
			if(Text[0]=='*')
			{
				// Start new layout.

				if(Text[1])
					Com_sprintf(cl.layout,sizeof(cl.layout),"%s",Text+1);
				else
					cl.layout[0]=0;
			}
			else
			{
				// Append to existing layout.

				if((strlen(cl.layout)+strlen(Text)+1)<sizeof(cl.layout))
				{
					strcat(cl.layout," ");
					strcat(cl.layout,Text);
				}
			}
		}
		*/
		else
		{
			Com_Printf ("%s", Text);
		}
	}
}
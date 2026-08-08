/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

/*****************************************************************************
 * name:		ai_main.c
 *
 * desc:		Quake3 bot AI
 *
 * $Archive: /MissionPack/code/game/ai_main.c $
 * $Author: osman $
 * $Revision: 1.5 $
 * $Modtime: 6/06/01 1:11p $
 * $Date: 2003/03/15 23:43:59 $
 *
 *****************************************************************************/


#include "g_local.h"
#include "qcommon/q_shared.h"
#include "botlib/botlib.h"		//bot lib interface
#include "botlib/be_aas.h"
#include "botlib/be_ea.h"
#include "botlib/be_ai_char.h"
#include "botlib/be_ai_chat.h"
#include "botlib/be_ai_gen.h"
#include "botlib/be_ai_goal.h"
#include "botlib/be_ai_move.h"
#include "botlib/be_ai_weap.h"
 //
#include "ai_main.h"
#include "w_saber.h"
//
#include "chars.h"
#include "inv.h"

/*
#define BOT_CTF_DEBUG	1
*/

#define BOT_THINK_TIME	0

//bot states
bot_state_t* botstates[MAX_CLIENTS];
//number of bots
int numbots;
//floating point time
float floattime;
//time to do a regular update
float regularupdate_time;
//

//for siege:
extern int rebel_attackers;
extern int imperial_attackers;

boteventtracker_t gBotEventTracker[MAX_CLIENTS];

//rww - new bot cvars..
vmCvar_t bot_forcepowers;
vmCvar_t bot_forgimmick;
vmCvar_t bot_honorableduelacceptance;
vmCvar_t bot_pvstype;
vmCvar_t bot_normgpath;
#ifndef FINAL_BUILD
vmCvar_t bot_getinthecarrr;
#endif

#ifdef _DEBUG
vmCvar_t bot_nogoals;
vmCvar_t bot_debugmessages;
#endif

vmCvar_t bot_attachments;
vmCvar_t bot_camp;

vmCvar_t bot_wp_info;
vmCvar_t bot_wp_edit;
vmCvar_t bot_wp_clearweight;
vmCvar_t bot_wp_distconnect;
vmCvar_t bot_wp_visconnect;
//end rww

wpobject_t* flagRed;
wpobject_t* oFlagRed;
wpobject_t* flagBlue;
wpobject_t* oFlagBlue;

gentity_t* eFlagRed;
gentity_t* droppedRedFlag;
gentity_t* eFlagBlue;
gentity_t* droppedBlueFlag;

char* ctfStateNames[] = {
	"CTFSTATE_NONE",
	"CTFSTATE_ATTACKER",
	"CTFSTATE_DEFENDER",
	"CTFSTATE_RETRIEVAL",
	"CTFSTATE_GUARDCARRIER",
	"CTFSTATE_GETFLAGHOME",
	"CTFSTATE_MAXCTFSTATES"
};

char* ctfStateDescriptions[] = {
	"I'm not occupied",
	"I'm attacking the enemy's base",
	"I'm defending our base",
	"I'm getting our flag back",
	"I'm escorting our flag carrier",
	"I've got the enemy's flag"
};

char* siegeStateDescriptions[] = {
	"I'm not occupied",
	"I'm attempting to complete the current objective",
	"I'm preventing the enemy from completing their objective"
};

char* teamplayStateDescriptions[] = {
	"I'm not occupied",
	"I'm following my squad commander",
	"I'm assisting my commanding",
	"I'm attempting to regroup and form a new squad"
};

static qboolean ValidBotClient(int client) { // Niksata Edit
	return (client >= 0 &&
		client < MAX_CLIENTS &&
		g_entities[client].inuse &&
		g_entities[client].client &&
		g_entities[client].client->pers.connected == CON_CONNECTED);
} // Niksata Edit

void BotStraightTPOrderCheck(gentity_t* ent, int ordernum, bot_state_t* bs)
{
	switch (ordernum)
	{
	case 0:
		if (bs->squadLeader == ent)
		{
			bs->teamplayState = 0;
			bs->squadLeader = NULL;
		}
		break;
	case TEAMPLAYSTATE_FOLLOWING:
		bs->teamplayState = ordernum;
		bs->isSquadLeader = 0;
		bs->squadLeader = ent;
		bs->wpDestSwitchTime = 0;
		break;
	case TEAMPLAYSTATE_ASSISTING:
		bs->teamplayState = ordernum;
		bs->isSquadLeader = 0;
		bs->squadLeader = ent;
		bs->wpDestSwitchTime = 0;
		break;
	default:
		bs->teamplayState = ordernum;
		break;
	}
}

void BotSelectWeapon(int client, int weapon)
{
	if (weapon <= WP_NONE)
	{
		//		assert(0);
		return;
	}
	trap->EA_SelectWeapon(client, weapon);
}

void BotReportStatus(bot_state_t* bs)
{
	if (level.gametype == GT_TEAM)
	{
		trap->EA_SayTeam(bs->client, teamplayStateDescriptions[bs->teamplayState]);
	}
	else if (level.gametype == GT_SIEGE)
	{
		trap->EA_SayTeam(bs->client, siegeStateDescriptions[bs->siegeState]);
	}
	else if (level.gametype == GT_CTF || level.gametype == GT_CTY)
	{
		trap->EA_SayTeam(bs->client, ctfStateDescriptions[bs->ctfState]);
	}
}

//accept a team order from a player
void BotOrder(gentity_t* ent, int clientnum, int ordernum)
{
	int stateMin = 0;
	int stateMax = 0;
	int i = 0;

	if (!ent || !ent->client || !ent->client->sess.teamLeader)
	{
		return;
	}

	if (clientnum != -1 && !botstates[clientnum])
	{
		return;
	}

	if (clientnum != -1 && !OnSameTeam(ent, &g_entities[clientnum]))
	{
		return;
	}

	if (level.gametype != GT_CTF && level.gametype != GT_CTY && level.gametype != GT_SIEGE && level.gametype != GT_TEAM)
	{
		return;
	}

	if (level.gametype == GT_CTF || level.gametype == GT_CTY)
	{
		stateMin = CTFSTATE_NONE;
		stateMax = CTFSTATE_MAXCTFSTATES;
	}
	else if (level.gametype == GT_SIEGE)
	{
		stateMin = SIEGESTATE_NONE;
		stateMax = SIEGESTATE_MAXSIEGESTATES;
	}
	else if (level.gametype == GT_TEAM)
	{
		stateMin = TEAMPLAYSTATE_NONE;
		stateMax = TEAMPLAYSTATE_MAXTPSTATES;
	}

	if ((ordernum < stateMin && ordernum != -1) || ordernum >= stateMax)
	{
		return;
	}

	if (clientnum != -1)
	{
		if (ordernum == -1)
		{
			BotReportStatus(botstates[clientnum]);
		}
		else
		{
			BotStraightTPOrderCheck(ent, ordernum, botstates[clientnum]);
			botstates[clientnum]->state_Forced = ordernum;
			botstates[clientnum]->chatObject = ent;
			botstates[clientnum]->chatAltObject = NULL;
			if (BotDoChat(botstates[clientnum], "OrderAccepted", 1))
			{
				botstates[clientnum]->chatTeam = 1;
			}
		}
	}
	else
	{
		while (i < MAX_CLIENTS)
		{
			if (botstates[i] && OnSameTeam(ent, &g_entities[i]))
			{
				if (ordernum == -1)
				{
					BotReportStatus(botstates[i]);
				}
				else
				{
					BotStraightTPOrderCheck(ent, ordernum, botstates[i]);
					botstates[i]->state_Forced = ordernum;
					botstates[i]->chatObject = ent;
					botstates[i]->chatAltObject = NULL;
					if (BotDoChat(botstates[i], "OrderAccepted", 0))
					{
						botstates[i]->chatTeam = 1;
					}
				}
			}

			i++;
		}
	}
}

//See if bot is mindtricked by the client in question
int BotMindTricked(int botClient, int enemyClient)
{
	forcedata_t* fd;

	if (!g_entities[enemyClient].client)
	{
		return 0;
	}

	fd = &g_entities[enemyClient].client->ps.fd;

	if (!fd)
	{
		return 0;
	}

	if (botClient > 47)
	{
		if (fd->forceMindtrickTargetIndex4 & (1 << (botClient - 48)))
		{
			return 1;
		}
	}
	else if (botClient > 31)
	{
		if (fd->forceMindtrickTargetIndex3 & (1 << (botClient - 32)))
		{
			return 1;
		}
	}
	else if (botClient > 15)
	{
		if (fd->forceMindtrickTargetIndex2 & (1 << (botClient - 16)))
		{
			return 1;
		}
	}
	else
	{
		if (fd->forceMindtrickTargetIndex & (1 << botClient))
		{
			return 1;
		}
	}

	return 0;
}

int BotGetWeaponRange(bot_state_t* bs);
int PassLovedOneCheck(bot_state_t* bs, gentity_t* ent);

void ExitLevel(void);

void QDECL BotAI_Print(int type, char* fmt, ...) { return; }

qboolean WP_ForcePowerUsable(gentity_t* self, forcePowers_t forcePower);

int IsTeamplay(void)
{
	if (level.gametype < GT_TEAM)
	{
		return 0;
	}

	return 1;
}

/*
==================
BotAI_GetClientState
==================
*/
int BotAI_GetClientState(int clientNum, playerState_t* state) {
	gentity_t* ent;

	ent = &g_entities[clientNum];
	if (!ent->inuse) {
		return qfalse;
	}
	if (!ent->client) {
		return qfalse;
	}

	memcpy(state, &ent->client->ps, sizeof(playerState_t));
	return qtrue;
}

/*
==================
BotAI_GetEntityState
==================
*/
int BotAI_GetEntityState(int entityNum, entityState_t* state) {
	gentity_t* ent;

	ent = &g_entities[entityNum];
	memset(state, 0, sizeof(entityState_t));
	if (!ent->inuse) return qfalse;
	if (!ent->r.linked) return qfalse;
	if (ent->r.svFlags & SVF_NOCLIENT) return qfalse;
	memcpy(state, &ent->s, sizeof(entityState_t));
	return qtrue;
}

/*
==================
BotAI_GetSnapshotEntity
==================
*/
int BotAI_GetSnapshotEntity(int clientNum, int sequence, entityState_t* state) {
	int		entNum;

	entNum = trap->BotGetSnapshotEntity(clientNum, sequence);
	if (entNum == -1) {
		memset(state, 0, sizeof(entityState_t));
		return -1;
	}

	BotAI_GetEntityState(entNum, state);

	return sequence + 1;
}

/*
==============
BotEntityInfo
==============
*/
void BotEntityInfo(int entnum, aas_entityinfo_t* info) {
	trap->AAS_EntityInfo(entnum, info);
}

/*
==============
NumBots
==============
*/
int NumBots(void) {
	return numbots;
}

/*
==============
AngleDifference
==============
*/
float AngleDifference(float ang1, float ang2) {
	float diff;

	diff = ang1 - ang2;
	if (ang1 > ang2) {
		if (diff > 180.0) diff -= 360.0;
	}
	else {
		if (diff < -180.0) diff += 360.0;
	}
	return diff;
}

/*
==============
BotChangeViewAngle
==============
*/
float BotChangeViewAngle(float angle, float ideal_angle, float speed) {
	float move;

	angle = AngleMod(angle);
	ideal_angle = AngleMod(ideal_angle);
	if (angle == ideal_angle) return angle;
	move = ideal_angle - angle;
	if (ideal_angle > angle) {
		if (move > 180.0) move -= 360.0;
	}
	else {
		if (move < -180.0) move += 360.0;
	}
	if (move > 0) {
		if (move > speed) move = speed;
	}
	else {
		if (move < -speed) move = -speed;
	}
	return AngleMod(angle + move);
}

/*
==============
BotChangeViewAngles
==============
*/
void BotChangeViewAngles(bot_state_t* bs, float thinktime) {
	float diff, factor, maxchange, anglespeed, disired_speed;
	int i;

	if (bs->ideal_viewangles[PITCH] > 180) bs->ideal_viewangles[PITCH] -= 360;

	if (bs->currentEnemy && bs->frame_Enemy_Vis)
	{
		if (bs->settings.skill <= 1)
		{
			factor = (bs->skills.turnspeed_combat * 0.4f) * bs->settings.skill;
		}
		else if (bs->settings.skill <= 2)
		{
			factor = (bs->skills.turnspeed_combat * 0.6f) * bs->settings.skill;
		}
		else if (bs->settings.skill <= 3)
		{
			factor = (bs->skills.turnspeed_combat * 0.8f) * bs->settings.skill;
		}
		else
		{
			factor = bs->skills.turnspeed_combat * bs->settings.skill;
		}
	}
	else
	{
		factor = bs->skills.turnspeed;
	}

	if (factor > 1)
		factor = 1;
	if (factor < 0.001)
		factor = 0.001f;

	maxchange = bs->skills.maxturn;

	if (g_newBotAI.integer) {
		maxchange = 1800;
	}

	//if (maxchange < 240) maxchange = 240;
	maxchange *= thinktime;
	for (i = 0; i < 2; i++) {
		bs->viewangles[i] = AngleMod(bs->viewangles[i]);
		bs->ideal_viewangles[i] = AngleMod(bs->ideal_viewangles[i]);
		diff = AngleDifference(bs->viewangles[i], bs->ideal_viewangles[i]);
		disired_speed = diff * factor;
		bs->viewanglespeed[i] += (bs->viewanglespeed[i] - disired_speed);
		if (bs->viewanglespeed[i] > 180) bs->viewanglespeed[i] = maxchange;
		if (bs->viewanglespeed[i] < -180) bs->viewanglespeed[i] = -maxchange;
		anglespeed = bs->viewanglespeed[i];
		if (anglespeed > maxchange) anglespeed = maxchange;
		if (anglespeed < -maxchange) anglespeed = -maxchange;
		bs->viewangles[i] += anglespeed;
		bs->viewangles[i] = AngleMod(bs->viewangles[i]);
		bs->viewanglespeed[i] *= 0.45 * (1 - factor);
	}
	if (bs->viewangles[PITCH] > 180) bs->viewangles[PITCH] -= 360;
	trap->EA_View(bs->client, bs->viewangles);
}

/*
==============
BotInputToUserCommand
==============
*/
void BotInputToUserCommand(bot_input_t* bi, usercmd_t* ucmd, int delta_angles[3], int time, int useTime) {
	vec3_t angles, forward, right;
	short temp;
	int j;
	float f, r, u, m;

	//clear the whole structure
	memset(ucmd, 0, sizeof(usercmd_t));
	//the duration for the user command in milli seconds
	ucmd->serverTime = time;
	//
	if (bi->actionflags & ACTION_DELAYEDJUMP) {
		bi->actionflags |= ACTION_JUMP;
		bi->actionflags &= ~ACTION_DELAYEDJUMP;
	}
	//set the buttons
	if (bi->actionflags & ACTION_RESPAWN) ucmd->buttons = BUTTON_ATTACK;
	if (bi->actionflags & ACTION_ATTACK) ucmd->buttons |= BUTTON_ATTACK;
	if (bi->actionflags & ACTION_ALT_ATTACK) ucmd->buttons |= BUTTON_ALT_ATTACK;
	//	if (bi->actionflags & ACTION_TALK) ucmd->buttons |= BUTTON_TALK;
	if (bi->actionflags & ACTION_GESTURE) ucmd->buttons |= BUTTON_GESTURE;
	if (bi->actionflags & ACTION_USE) ucmd->buttons |= BUTTON_USE_HOLDABLE;
	if (bi->actionflags & ACTION_WALK) ucmd->buttons |= BUTTON_WALKING;

	if (bi->actionflags & ACTION_FORCEPOWER) ucmd->buttons |= BUTTON_FORCEPOWER;

	if (bi->actionflags & ACTION_SKI) ucmd->buttons |= BUTTON_DASH;

	if (useTime < level.time && Q_irand(1, 10) < 5)
	{ //for now just hit use randomly in case there's something useable around
		ucmd->buttons |= BUTTON_USE;
	}

#if 0
	// Here's an interesting bit.  The bots in TA used buttons to do additional gestures.
	// I ripped them out because I didn't want too many buttons given the fact that I was already adding some for JK2.
	// We can always add some back in if we want though.
	if (bi->actionflags & ACTION_AFFIRMATIVE) ucmd->buttons |= BUTTON_AFFIRMATIVE;
	if (bi->actionflags & ACTION_NEGATIVE) ucmd->buttons |= BUTTON_NEGATIVE;
	if (bi->actionflags & ACTION_GETFLAG) ucmd->buttons |= BUTTON_GETFLAG;
	if (bi->actionflags & ACTION_GUARDBASE) ucmd->buttons |= BUTTON_GUARDBASE;
	if (bi->actionflags & ACTION_PATROL) ucmd->buttons |= BUTTON_PATROL;
	if (bi->actionflags & ACTION_FOLLOWME) ucmd->buttons |= BUTTON_FOLLOWME;
#endif //0

	if (bi->weapon == WP_NONE)
	{
#ifdef _DEBUG
		//		Com_Printf("WARNING: Bot tried to use WP_NONE!\n");
#endif
		bi->weapon = WP_BRYAR_PISTOL;
	}

	//
	ucmd->weapon = bi->weapon;
	//set the view angles
	//NOTE: the ucmd->angles are the angles WITHOUT the delta angles
	ucmd->angles[PITCH] = ANGLE2SHORT(bi->viewangles[PITCH]);
	ucmd->angles[YAW] = ANGLE2SHORT(bi->viewangles[YAW]);
	ucmd->angles[ROLL] = ANGLE2SHORT(bi->viewangles[ROLL]);
	//subtract the delta angles
	for (j = 0; j < 3; j++) {
		temp = ucmd->angles[j] - delta_angles[j];
		ucmd->angles[j] = temp;
	}
	//NOTE: movement is relative to the REAL view angles
	//get the horizontal forward and right vector
	//get the pitch in the range [-180, 180]
	if (bi->dir[2]) angles[PITCH] = bi->viewangles[PITCH];
	else angles[PITCH] = 0;
	angles[YAW] = bi->viewangles[YAW];
	angles[ROLL] = 0;
	AngleVectors(angles, forward, right, NULL);
	//bot input speed is in the range [0, 400]
	bi->speed = bi->speed * 127 / 400;
	//set the view independent movement
	f = DotProduct(forward, bi->dir);
	r = DotProduct(right, bi->dir);
	u = 0; // Niksata Edit - default -- u = fabs(forward[2]) * bi->dir[2]; -- crouch/jump spam source
	m = fabs(f);

	if (fabs(r) > m) {
		m = fabs(r);
	}

	if (fabs(u) > m) {
		m = fabs(u);
	}

	if (m > 0) {
		f *= bi->speed / m;
		r *= bi->speed / m;
		u *= bi->speed / m;
	}

	ucmd->forwardmove = f;
	ucmd->rightmove = r;
	ucmd->upmove = u;
	//normal keyboard movement
	if (bi->actionflags & ACTION_MOVEFORWARD) ucmd->forwardmove = 127;
	if (bi->actionflags & ACTION_MOVEBACK) ucmd->forwardmove = -127;
	if (bi->actionflags & ACTION_MOVELEFT) ucmd->rightmove = -127;
	if (bi->actionflags & ACTION_MOVERIGHT) ucmd->rightmove = 127;
	//jump/moveup
	if (bi->actionflags & ACTION_JUMP) ucmd->upmove = 127;
	//crouch/movedown
	if (bi->actionflags & ACTION_CROUCH) {
		ucmd->upmove = -127;
	}
}

/*
==============
BotUpdateInput
==============
*/
void BotUpdateInput(bot_state_t* bs, int time, int elapsed_time) {
	bot_input_t bi;
	int j;

	//add the delta angles to the bot's current view angles
	for (j = 0; j < 3; j++) {
		bs->viewangles[j] = AngleMod(bs->viewangles[j] + SHORT2ANGLE(bs->cur_ps.delta_angles[j]));
	}
	//change the bot view angles
	BotChangeViewAngles(bs, (float)elapsed_time / 1000);
	//retrieve the bot input
	trap->EA_GetInput(bs->client, (float)time / 1000, &bi);
	//respawn hack
	if (bi.actionflags & ACTION_RESPAWN) {
		if (bs->lastucmd.buttons & BUTTON_ATTACK) bi.actionflags &= ~(ACTION_RESPAWN | ACTION_ATTACK);
	}
	//convert the bot input to a usercmd
	BotInputToUserCommand(&bi, &bs->lastucmd, bs->cur_ps.delta_angles, time, bs->noUseTime);
	//subtract the delta angles
	for (j = 0; j < 3; j++) {
		bs->viewangles[j] = AngleMod(bs->viewangles[j] - SHORT2ANGLE(bs->cur_ps.delta_angles[j]));
	}
}

/*
==============
BotAIRegularUpdate
==============
*/
void BotAIRegularUpdate(void) {
	if (regularupdate_time < FloatTime()) {
		trap->BotUpdateEntityItems();
		regularupdate_time = FloatTime() + 0.3;
	}
}

/*
==============
RemoveColorEscapeSequences
==============
*/
void RemoveColorEscapeSequences(char* text) {
	int i, l;

	l = 0;
	for (i = 0; text[i]; i++) {
		if (Q_IsColorStringExt(&text[i])) {
			i++;
			continue;
		}
		if (text[i] > 0x7E)
			continue;
		text[l++] = text[i];
	}
	text[l] = '\0';
}


/*
==============
BotAI
==============
*/
int BotAI(int client, float thinktime) {
	bot_state_t* bs;

	// ADD BOUNDS CHECK:
	if (client < 0 || client >= MAX_CLIENTS) {
		BotAI_Print(PRT_ERROR, "BotAI: Invalid client index %d\n", client);
		return qfalse;
	}

	bs = botstates[client];
	if (!bs || !bs->inuse) {
		BotAI_Print(PRT_FATAL, "BotAI: client %d is not setup\n", client);
		return qfalse;
	}

	// ADD ENTITY VALIDATION:
	if (!g_entities[client].inuse || !g_entities[client].client) {
		BotAI_Print(PRT_ERROR, "BotAI: client %d entity not valid\n", client);
		return qfalse;
	}

	char buf[1024], * args;
	int j;
#ifdef _DEBUG
	int start = 0;
	int end = 0;
#endif

	trap->EA_ResetInput(client);
	//
	bs = botstates[client];
	if (!bs || !bs->inuse) {
		BotAI_Print(PRT_FATAL, "BotAI: client %d is not setup\n", client);
		return qfalse;
	}

	//retrieve the current client state
	BotAI_GetClientState(client, &bs->cur_ps);

	//retrieve any waiting server commands
	while (trap->BotGetServerCommand(client, buf, sizeof(buf))) {
		//have buf point to the command and args to the command arguments
		args = strchr(buf, ' ');
		if (!args) continue;
		*args++ = '\0';

		//remove color espace sequences from the arguments
		RemoveColorEscapeSequences(args);

		if (!Q_stricmp(buf, "cp "))
		{ /*CenterPrintf*/
		}
		else if (!Q_stricmp(buf, "cs"))
		{ /*ConfigStringModified*/
		}
		else if (!Q_stricmp(buf, "scores"))
		{ /*FIXME: parse scores?*/
		}
		else if (!Q_stricmp(buf, "clientLevelShot"))
		{ /*ignore*/
		}
	}
	//add the delta angles to the bot's current view angles
	for (j = 0; j < 3; j++) {
		bs->viewangles[j] = AngleMod(bs->viewangles[j] + SHORT2ANGLE(bs->cur_ps.delta_angles[j]));
	}
	//increase the local time of the bot
	bs->ltime += thinktime;
	//
	bs->thinktime = thinktime;
	//origin of the bot
	VectorCopy(bs->cur_ps.origin, bs->origin);
	//eye coordinates of the bot
	VectorCopy(bs->cur_ps.origin, bs->eye);
	bs->eye[2] += bs->cur_ps.viewheight;
	//get the area the bot is in

#ifdef _DEBUG
	start = trap->Milliseconds();
#endif
	if (g_newBotAI.integer)
		NewBotAI(bs, thinktime);
	else
		StandardBotAI(bs, thinktime);
#ifdef _DEBUG
	end = trap->Milliseconds();

	trap->Cvar_Update(&bot_debugmessages);

	if (bot_debugmessages.integer)
	{
		Com_Printf("Single AI frametime: %i\n", (end - start));
	}
#endif

	//subtract the delta angles
	for (j = 0; j < 3; j++) {
		bs->viewangles[j] = AngleMod(bs->viewangles[j] - SHORT2ANGLE(bs->cur_ps.delta_angles[j]));
	}
	//everything was ok
	return qtrue;
}

/*
==================
BotScheduleBotThink
==================
*/
void BotScheduleBotThink(void) {
	int i, botnum;

	botnum = 0;

	for (i = 0; i < MAX_CLIENTS; i++) {
		if (!botstates[i] || !botstates[i]->inuse) {
			continue;
		}
		//initialize the bot think residual time
		botstates[i]->botthink_residual = BOT_THINK_TIME * botnum / numbots;
		botnum++;
	}
}

int PlayersInGame(void)
{
	int i = 0;
	gentity_t* ent;
	int pl = 0;

	while (i < MAX_CLIENTS)
	{
		ent = &g_entities[i];

		if (ent && ent->client && ent->client->pers.connected == CON_CONNECTED)
		{
			pl++;
		}

		i++;
	}

	return pl;
}

/*
==============
BotAISetupClient
==============
*/
int BotAISetupClient(int client, struct bot_settings_s* settings, qboolean restart) { // Niksata Edit
	bot_state_t* bs;

	if (client < 0 || client >= MAX_CLIENTS) {
		BotAI_Print(PRT_FATAL, "BotAISetupClient: Invalid client index %d\n", client);
		return qfalse;
	}

	// CHECK FIRST before allocating new memory
	if (botstates[client] && botstates[client]->inuse) {
		BotAI_Print(PRT_ERROR, "BotAISetupClient: client %d already setup\n", client);
		return qfalse;  // Not fatal, just return
	}

	// Entity validation
	if (!g_entities[client].inuse || !g_entities[client].client) {
		BotAI_Print(PRT_ERROR, "BotAISetupClient: Entity %d not valid\n", client);
		return qfalse;
	}

	// Now allocate new memory
	botstates[client] = (bot_state_t*)B_Alloc(sizeof(bot_state_t));
	if (!botstates[client]) {
		BotAI_Print(PRT_FATAL, "BotAISetupClient: Failed to allocate memory for bot %d\n", client);
		return qfalse;
	}
	memset(botstates[client], 0, sizeof(bot_state_t));

	bs = botstates[client];

	memcpy(&bs->settings, settings, sizeof(bot_settings_t));

	bs->client = client; //need to know the client number before doing personality stuff

	//initialize weapon weight defaults..
	bs->botWeaponWeights[WP_NONE] = 0;
	bs->botWeaponWeights[WP_STUN_BATON] = 1;
	bs->botWeaponWeights[WP_SABER] = 10;
	bs->botWeaponWeights[WP_BRYAR_PISTOL] = 11;
	bs->botWeaponWeights[WP_BLASTER] = 12;
	bs->botWeaponWeights[WP_DISRUPTOR] = 13;
	bs->botWeaponWeights[WP_BOWCASTER] = 14;
	bs->botWeaponWeights[WP_REPEATER] = 15;
	bs->botWeaponWeights[WP_DEMP2] = 16;
	bs->botWeaponWeights[WP_FLECHETTE] = 17;
	bs->botWeaponWeights[WP_ROCKET_LAUNCHER] = 18;
	bs->botWeaponWeights[WP_THERMAL] = 14;
	bs->botWeaponWeights[WP_TRIP_MINE] = 0;
	bs->botWeaponWeights[WP_DET_PACK] = 0;
	bs->botWeaponWeights[WP_MELEE] = 1;

	BotUtilizePersonality(bs);

	if (level.gametype == GT_DUEL || level.gametype == GT_POWERDUEL)
	{
		bs->botWeaponWeights[WP_SABER] = 13;
	}

	//allocate a goal state
	bs->gs = trap->BotAllocGoalState(client);

	//allocate a weapon state
	bs->ws = trap->BotAllocWeaponState();

	bs->inuse = qtrue;
	bs->entitynum = client;
	bs->setupcount = 4;
	bs->entergame_time = FloatTime();
	bs->ms = trap->BotAllocMoveState();
	numbots++;

	//NOTE: reschedule the bot thinking
	BotScheduleBotThink();

	// Initialize perfect blocking system // Niksata Edit
	Bot_InitPerfectBlocking(bs); // Niksata Edit

	// Initialize movement system // Niksata Edit
	bs->swingMemoryLeft = 0.0f;
	bs->swingMemoryRight = 0.0f;
	bs->swingMemoryBack = 0.0f;
	bs->forceMove_Forward = 0;
	bs->forceMove_Right = 0;
	bs->forceMove_Up = 0;
	bs->noUseTime = 0;
	bs->strafeExpireTime = 0;
	bs->strafeDir = 0;
	bs->lastKataTime = 0;
	bs->kataStreak = 0;
	bs->styleSwitchTime = level.time + Q_irand(3000, 8000); // Niksata Edit

	// Initialize enhanced combat systems
	bs->nextAttackTime = 0;
	bs->attackRecoveryTime = 0;
	bs->lastAttackTime = 0;
	bs->lastAttackType = 0;
	bs->comboWindow = 0;
	bs->lastComboTime = 0;
	bs->lastAttackHitTime = 0;
	bs->nextSpecialMoveTime = 0;
	bs->lastSpecialMoveTime = 0;
	bs->combatStrafeTime = 0;
	bs->combatStrafeDir = 0;
	bs->isAttacking = qfalse;

	// Initialize movement prediction
	VectorClear(bs->lastEnemyVelocity);
	bs->enemyMovementHistoryTime = 0;
	bs->lastEnemyDistance = 0;

	if (PlayersInGame())
	{ //don't talk to yourself
		BotDoChat(bs, "GeneralGreetings", 0);
	}

	return qtrue;
}

/*
==============
BotAIShutdownClient
==============
*/
int BotAIShutdownClient(int client, qboolean restart) {
	bot_state_t* bs;

	if (client < 0 || client >= MAX_CLIENTS) {
		return qfalse;
	}

	bs = botstates[client];
	if (!bs || !bs->inuse) {
		// Don't print error - this is normal during shutdown
		return qfalse;
	}

	bs = botstates[client];
	if (!bs || !bs->inuse) {
		//BotAI_Print(PRT_ERROR, "BotAIShutdownClient: client %d already shutdown\n", client);
		return qfalse;
	}

	trap->BotFreeMoveState(bs->ms);
	//free the goal state`
	trap->BotFreeGoalState(bs->gs);
	//free the weapon weights
	trap->BotFreeWeaponState(bs->ws);
	//
	//clear the bot state
	memset(bs, 0, sizeof(bot_state_t));
	//set the inuse flag to qfalse
	bs->inuse = qfalse;
	//there's one bot less
	numbots--;
	//everything went ok
	return qtrue;
}

/*
==============
BotResetState

called when a bot enters the intermission or observer mode and
when the level is changed
==============
*/
void BotResetState(bot_state_t* bs) {
	int client, entitynum, inuse;
	int movestate, goalstate, weaponstate;
	bot_settings_t settings;
	playerState_t ps;							//current player state
	float entergame_time;

	//save some things that should not be reset here
	memcpy(&settings, &bs->settings, sizeof(bot_settings_t));
	memcpy(&ps, &bs->cur_ps, sizeof(playerState_t));
	inuse = bs->inuse;
	client = bs->client;
	entitynum = bs->entitynum;
	movestate = bs->ms;
	goalstate = bs->gs;
	weaponstate = bs->ws;
	entergame_time = bs->entergame_time;
	//reset the whole state
	memset(bs, 0, sizeof(bot_state_t));
	//copy back some state stuff that should not be reset
	bs->ms = movestate;
	bs->gs = goalstate;
	bs->ws = weaponstate;
	memcpy(&bs->cur_ps, &ps, sizeof(playerState_t));
	memcpy(&bs->settings, &settings, sizeof(bot_settings_t));
	bs->inuse = inuse;
	bs->client = client;
	bs->entitynum = entitynum;
	bs->entergame_time = entergame_time;
	//reset several states
	if (bs->ms) trap->BotResetMoveState(bs->ms);
	if (bs->gs) trap->BotResetGoalState(bs->gs);
	if (bs->ws) trap->BotResetWeaponState(bs->ws);
	if (bs->gs) trap->BotResetAvoidGoals(bs->gs);
	if (bs->ms) trap->BotResetAvoidReach(bs->ms);
}

/*
==============
BotAILoadMap
==============
*/
int BotAILoadMap(int restart) {
	int			i;

	for (i = 0; i < MAX_CLIENTS; i++) {
		if (botstates[i] && botstates[i]->inuse) {
			BotResetState(botstates[i]);
			botstates[i]->setupcount = 4;
		}
	}

	return qtrue;
}

//rww - bot ai

//standard visibility check
int OrgVisible(vec3_t org1, vec3_t org2, int ignore)
{
	trace_t tr;

	JP_Trace(&tr, org1, NULL, NULL, org2, ignore, MASK_SOLID, qfalse, 0, 0);

	if (tr.fraction == 1)
	{
		return 1;
	}

	return 0;
}

//special waypoint visibility check
int WPOrgVisible(gentity_t* bot, vec3_t org1, vec3_t org2, int ignore)
{
	trace_t tr;
	gentity_t* ownent;

	JP_Trace(&tr, org1, NULL, NULL, org2, ignore, MASK_SOLID, qfalse, 0, 0);

	if (tr.fraction == 1)
	{
		JP_Trace(&tr, org1, NULL, NULL, org2, ignore, MASK_PLAYERSOLID, qfalse, 0, 0);

		if (tr.fraction != 1 && tr.entityNum != ENTITYNUM_NONE && g_entities[tr.entityNum].s.eType == ET_SPECIAL)
		{
			if (g_entities[tr.entityNum].parent && g_entities[tr.entityNum].parent->client)
			{
				ownent = g_entities[tr.entityNum].parent;

				if (OnSameTeam(bot, ownent) || bot->s.number == ownent->s.number)
				{
					return 1;
				}
			}
			return 2;
		}

		return 1;
	}

	return 0;
}

//visibility check with hull trace
int OrgVisibleBox(vec3_t org1, vec3_t mins, vec3_t maxs, vec3_t org2, int ignore)
{
	trace_t tr;

	if (RMG.integer)
	{
		JP_Trace(&tr, org1, NULL, NULL, org2, ignore, MASK_SOLID, qfalse, 0, 0);
	}
	else
	{
		JP_Trace(&tr, org1, mins, maxs, org2, ignore, MASK_SOLID, qfalse, 0, 0);
	}

	if (tr.fraction == 1 && !tr.startsolid && !tr.allsolid)
	{
		return 1;
	}

	return 0;
}

//see if there's a func_* ent under the given pos.
//kind of badly done, but this shouldn't happen
//often.
int CheckForFunc(vec3_t org, int ignore)
{
	gentity_t* fent;
	vec3_t under;
	trace_t tr;

	VectorCopy(org, under);

	under[2] -= 64;

	JP_Trace(&tr, org, NULL, NULL, under, ignore, MASK_SOLID, qfalse, 0, 0);

	if (tr.fraction == 1)
	{
		return 0;
	}

	fent = &g_entities[tr.entityNum];

	if (!fent)
	{
		return 0;
	}

	if (strstr(fent->classname, "func_"))
	{
		return 1; //there's a func brush here
	}

	return 0;
}

//perform pvs check based on rmg or not
qboolean BotPVSCheck(const vec3_t p1, const vec3_t p2)
{
	if (RMG.integer && bot_pvstype.integer)
	{
		vec3_t subPoint;
		VectorSubtract(p1, p2, subPoint);

		if (VectorLength(subPoint) > 5000)
		{
			return qfalse;
		}
		return qtrue;
	}

	return trap->InPVS(p1, p2);
}

//get the index to the nearest visible waypoint in the global trail
int GetNearestVisibleWP(vec3_t org, int ignore)
{
	int i;
	float bestdist;
	float flLen;
	int bestindex;
	vec3_t a, mins, maxs;

	i = 0;
	if (RMG.integer)
	{
		bestdist = 300;
	}
	else
	{
		bestdist = 800;//99999;
		//don't trace over 800 units away to avoid GIANT HORRIBLE SPEED HITS ^_^
	}
	bestindex = -1;

	mins[0] = -15;
	mins[1] = -15;
	mins[2] = -1;
	maxs[0] = 15;
	maxs[1] = 15;
	maxs[2] = 1;

	while (i < gWPNum)
	{
		if (gWPArray[i] && gWPArray[i]->inuse)
		{
			VectorSubtract(org, gWPArray[i]->origin, a);
			flLen = VectorLength(a);

			if (flLen < bestdist && (RMG.integer || BotPVSCheck(org, gWPArray[i]->origin)) && OrgVisibleBox(org, mins, maxs, gWPArray[i]->origin, ignore))
			{
				bestdist = flLen;
				bestindex = i;
			}
		}

		i++;
	}

	return bestindex;
}

//wpDirection
//0 == FORWARD
//1 == BACKWARD

//see if this is a valid waypoint to pick up in our
//current state (whatever that may be)
int PassWayCheck(bot_state_t* bs, int windex)
{
	if (!gWPArray[windex] || !gWPArray[windex]->inuse)
	{ //bad point index
		return 0;
	}

	if (RMG.integer)
	{
		if ((gWPArray[windex]->flags & WPFLAG_RED_FLAG) ||
			(gWPArray[windex]->flags & WPFLAG_BLUE_FLAG))
		{ //red or blue flag, we'd like to get here
			return 1;
		}
	}

	if (bs->wpDirection && (gWPArray[windex]->flags & WPFLAG_ONEWAY_FWD))
	{ //we're not travelling in a direction on the trail that will allow us to pass this point
		return 0;
	}
	else if (!bs->wpDirection && (gWPArray[windex]->flags & WPFLAG_ONEWAY_BACK))
	{ //we're not travelling in a direction on the trail that will allow us to pass this point
		return 0;
	}

	if (bs->wpCurrent && gWPArray[windex]->forceJumpTo &&
		gWPArray[windex]->origin[2] > (bs->wpCurrent->origin[2] + 64) &&
		bs->cur_ps.fd.forcePowerLevel[FP_LEVITATION] < gWPArray[windex]->forceJumpTo)
	{ //waypoint requires force jump level greater than our current one to pass
		return 0;
	}

	return 1;
}

//tally up the distance between two waypoints
float TotalTrailDistance(int start, int end, bot_state_t* bs)
{
	int beginat;
	int endat;
	float distancetotal;

	distancetotal = 0;

	if (start > end)
	{
		beginat = end;
		endat = start;
	}
	else
	{
		beginat = start;
		endat = end;
	}

	while (beginat < endat)
	{
		if (beginat >= gWPNum || !gWPArray[beginat] || !gWPArray[beginat]->inuse)
		{ //invalid waypoint index
			return -1;
		}

		if (!RMG.integer)
		{
			if ((end > start && gWPArray[beginat]->flags & WPFLAG_ONEWAY_BACK) ||
				(start > end && gWPArray[beginat]->flags & WPFLAG_ONEWAY_FWD))
			{ //a one-way point, this means this path cannot be travelled to the final point
				return -1;
			}
		}

#if 0 //disabled force jump checks for now
		if (gWPArray[beginat]->forceJumpTo)
		{
			if (gWPArray[beginat - 1] && gWPArray[beginat - 1]->origin[2] + 64 < gWPArray[beginat]->origin[2])
			{
				gdif = gWPArray[beginat]->origin[2] - gWPArray[beginat - 1]->origin[2];
			}

			if (gdif)
			{
				if (bs && bs->cur_ps.fd.forcePowerLevel[FP_LEVITATION] < gWPArray[beginat]->forceJumpTo)
				{
					return -1;
				}
			}
		}

		if (bs->wpCurrent && gWPArray[windex]->forceJumpTo &&
			gWPArray[windex]->origin[2] > (bs->wpCurrent->origin[2] + 64) &&
			bs->cur_ps.fd.forcePowerLevel[FP_LEVITATION] < gWPArray[windex]->forceJumpTo)
		{
			return -1;
		}
#endif

		distancetotal += gWPArray[beginat]->disttonext;

		beginat++;
	}

	return distancetotal;
}

//see if there's a route shorter than our current one to get
//to the final destination we currently desire
void CheckForShorterRoutes(bot_state_t* bs, int newwpindex)
{
	float bestlen;
	float checklen;
	int bestindex;
	int i;
	int fj;

	i = 0;
	fj = 0;

	if (!bs->wpDestination)
	{
		return;
	}

	//set our traversal direction based on the index of the point
	if (newwpindex < bs->wpDestination->index)
	{
		bs->wpDirection = 0;
	}
	else if (newwpindex > bs->wpDestination->index)
	{
		bs->wpDirection = 1;
	}

	//can't switch again yet
	if (bs->wpSwitchTime > level.time)
	{
		return;
	}

	//no neighboring points to check off of
	if (!gWPArray[newwpindex]->neighbornum)
	{
		return;
	}

	//get the trail distance for our wp
	bestindex = newwpindex;
	bestlen = TotalTrailDistance(newwpindex, bs->wpDestination->index, bs);

	while (i < gWPArray[newwpindex]->neighbornum)
	{ //now go through the neighbors and check the distance to the desired point from each neighbor
		checklen = TotalTrailDistance(gWPArray[newwpindex]->neighbors[i].num, bs->wpDestination->index, bs);

		if (checklen < bestlen - 64 || bestlen == -1)
		{ //this path covers less distance, let's take it instead
			if (bs->cur_ps.fd.forcePowerLevel[FP_LEVITATION] >= gWPArray[newwpindex]->neighbors[i].forceJumpTo)
			{
				bestlen = checklen;
				bestindex = gWPArray[newwpindex]->neighbors[i].num;

				if (gWPArray[newwpindex]->neighbors[i].forceJumpTo)
				{
					fj = gWPArray[newwpindex]->neighbors[i].forceJumpTo;
				}
				else
				{
					fj = 0;
				}
			}
		}

		i++;
	}

	if (bestindex != newwpindex && bestindex != -1)
	{ //we found a path we want to switch to, let's do it
		bs->wpCurrent = gWPArray[bestindex];
		bs->wpSwitchTime = level.time + 3000;

		if (fj)
		{ //do we have to force jump to get to this neighbor?
#ifndef FORCEJUMP_INSTANTMETHOD
			bs->forceJumpChargeTime = level.time + 1000;
			bs->beStill = level.time + 1000;
			bs->forceJumping = bs->forceJumpChargeTime;
#else
			bs->beStill = level.time + 500;
			bs->jumpTime = level.time + fj * 1200;
			bs->jDelay = level.time + 200;
			bs->forceJumping = bs->jumpTime;
#endif
		}
	}
}

//check for flags on the waypoint we're currently travelling to
//and perform the desired behavior based on the flag
void WPConstantRoutine(bot_state_t* bs)
{
	if (!bs->wpCurrent)
	{
		return;
	}

	if (bs->wpCurrent->flags & WPFLAG_DUCK)
	{ //duck while travelling to this point
		bs->duckTime = level.time + 100;
	}

#ifndef FORCEJUMP_INSTANTMETHOD
	if (bs->wpCurrent->flags & WPFLAG_JUMP)
	{ //jump while travelling to this point
		float heightDif = (bs->wpCurrent->origin[2] - bs->origin[2] + 16);

		if (bs->origin[2] + 16 >= bs->wpCurrent->origin[2])
		{ //don't need to jump, we're already higher than this point
			heightDif = 0;
		}

		if (heightDif > 40 && (bs->cur_ps.fd.forcePowersKnown & (1 << FP_LEVITATION)) && (bs->cur_ps.fd.forceJumpCharge < (forceJumpStrength[bs->cur_ps.fd.forcePowerLevel[FP_LEVITATION]] - 100) || bs->cur_ps.groundEntityNum == ENTITYNUM_NONE))
		{ //alright, let's jump
			bs->forceJumpChargeTime = level.time + 1000;
			if (bs->cur_ps.groundEntityNum != ENTITYNUM_NONE && bs->jumpPrep < (level.time - 300))
			{
				bs->jumpPrep = level.time + 700;
			}
			bs->beStill = level.time + 300;
			bs->jumpTime = 0;

			if (bs->wpSeenTime < (level.time + 600))
			{
				bs->wpSeenTime = level.time + 600;
			}
		}
		else if (heightDif > 64 && !(bs->cur_ps.fd.forcePowersKnown & (1 << FP_LEVITATION)))
		{ //this point needs force jump to reach and we don't have it
			//Kill the current point and turn around
			bs->wpCurrent = NULL;
			if (bs->wpDirection)
			{
				bs->wpDirection = 0;
			}
			else
			{
				bs->wpDirection = 1;
			}

			return;
		}
	}
#endif

	if (bs->wpCurrent->forceJumpTo)
	{
#ifdef FORCEJUMP_INSTANTMETHOD
		if (bs->origin[2] + 16 < bs->wpCurrent->origin[2])
		{
			bs->jumpTime = level.time + 100;
		}
#else

		if (bs->cur_ps.fd.forceJumpCharge < (forceJumpStrength[bs->cur_ps.fd.forcePowerLevel[FP_LEVITATION]] - 100))
		{
			bs->forceJumpChargeTime = level.time + 200;
		}
#endif
	}
}

//check if our ctf state is to guard the base
qboolean BotCTFGuardDuty(bot_state_t* bs)
{
	if (level.gametype != GT_CTF && level.gametype != GT_CTY)
	{
		return qfalse;
	}

	if (bs->ctfState == CTFSTATE_DEFENDER)
	{
		return qtrue;
	}

	return qfalse;
}

//when we reach the waypoint we are travelling to,
//this function will be called. We will perform any
//checks for flags on the current wp and activate
//any "touch" events based on that.
void WPTouchRoutine(bot_state_t* bs)
{
	int lastNum;

	if (!bs->wpCurrent)
	{
		return;
	}

	bs->wpTravelTime = level.time + 10000;

	if (bs->wpCurrent->flags & WPFLAG_NOMOVEFUNC)
	{ //don't try to use any nearby map objects for a little while
		bs->noUseTime = level.time + 4000;
	}

#ifdef FORCEJUMP_INSTANTMETHOD
	if ((bs->wpCurrent->flags & WPFLAG_JUMP) && bs->wpCurrent->forceJumpTo)
	{ //jump if we're flagged to but not if this indicates a force jump point. Force jumping is
	  //handled elsewhere.
		bs->jumpTime = level.time + 100;
	}
#else
	if ((bs->wpCurrent->flags & WPFLAG_JUMP) && !bs->wpCurrent->forceJumpTo)
	{ //jump if we're flagged to but not if this indicates a force jump point. Force jumping is
	  //handled elsewhere.
		bs->jumpTime = level.time + 100;
	}
#endif

	if (bs->isCamper && bot_camp.integer && (BotIsAChickenWuss(bs) || BotCTFGuardDuty(bs) || bs->isCamper == 2) && ((bs->wpCurrent->flags & WPFLAG_SNIPEORCAMP) || (bs->wpCurrent->flags & WPFLAG_SNIPEORCAMPSTAND)) &&
		bs->cur_ps.weapon != WP_SABER && bs->cur_ps.weapon != WP_MELEE && bs->cur_ps.weapon != WP_STUN_BATON)
	{ //if we're a camper and a chicken then camp
		if (bs->wpDirection)
		{
			lastNum = bs->wpCurrent->index + 1;
		}
		else
		{
			lastNum = bs->wpCurrent->index - 1;
		}

		if (gWPArray[lastNum] && gWPArray[lastNum]->inuse && gWPArray[lastNum]->index && bs->isCamping < level.time)
		{
			bs->isCamping = level.time + rand() % 15000 + 30000;
			bs->wpCamping = bs->wpCurrent;
			bs->wpCampingTo = gWPArray[lastNum];

			if (bs->wpCurrent->flags & WPFLAG_SNIPEORCAMPSTAND)
			{
				bs->campStanding = qtrue;
			}
			else
			{
				bs->campStanding = qfalse;
			}
		}

	}
	else if ((bs->cur_ps.weapon == WP_SABER || bs->cur_ps.weapon == WP_STUN_BATON || bs->cur_ps.weapon == WP_MELEE) &&
		bs->isCamping > level.time)
	{ //don't snipe/camp with a melee weapon, that would be silly
		bs->isCamping = 0;
		bs->wpCampingTo = NULL;
		bs->wpCamping = NULL;
	}

	if (bs->wpDestination)
	{
		if (bs->wpCurrent->index == bs->wpDestination->index)
		{
			bs->wpDestination = NULL;

			if (bs->runningLikeASissy)
			{ //this obviously means we're scared and running, so we'll want to keep our navigational priorities less delayed
				bs->destinationGrabTime = level.time + 500;
			}
			else
			{
				bs->destinationGrabTime = level.time + 3500;
			}
		}
		else
		{
			CheckForShorterRoutes(bs, bs->wpCurrent->index);
		}
	}
}

//could also slowly lerp toward, but for now
//just copying straight over.
void MoveTowardIdealAngles(bot_state_t* bs)
{
	VectorCopy(bs->goalAngles, bs->ideal_viewangles);
}

#define BOT_STRAFE_AVOIDANCE

#ifdef BOT_STRAFE_AVOIDANCE
#define STRAFEAROUND_RIGHT			1
#define STRAFEAROUND_LEFT			2

//do some trace checks for strafing to get an idea of where we
//are and if we should move to avoid obstacles.
int BotTrace_Strafe(bot_state_t* bs, vec3_t traceto)
{
	vec3_t playerMins = { -15, -15, /*DEFAULT_MINS_2*/-8 };
	vec3_t playerMaxs = { 15, 15, DEFAULT_MAXS_2 };
	vec3_t from, to;
	vec3_t dirAng, dirDif;
	vec3_t forward, right;
	trace_t tr;

	if (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE)
	{ //don't do this in the air, it can be.. dangerous.
		return 0;
	}

	VectorSubtract(traceto, bs->origin, dirAng);
	VectorNormalize(dirAng);
	vectoangles(dirAng, dirAng);

	if (AngleDifference(bs->viewangles[YAW], dirAng[YAW]) > 60 ||
		AngleDifference(bs->viewangles[YAW], dirAng[YAW]) < -60)
	{ //If we aren't facing the direction we're going here, then we've got enough excuse to be too stupid to strafe around anyway
		return 0;
	}

	VectorCopy(bs->origin, from);
	VectorCopy(traceto, to);

	VectorSubtract(to, from, dirDif);
	VectorNormalize(dirDif);
	vectoangles(dirDif, dirDif);

	AngleVectors(dirDif, forward, 0, 0);

	to[0] = from[0] + forward[0] * 32;
	to[1] = from[1] + forward[1] * 32;
	to[2] = from[2] + forward[2] * 32;

	JP_Trace(&tr, from, playerMins, playerMaxs, to, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

	if (tr.fraction == 1)
	{
		return 0;
	}

	AngleVectors(dirAng, 0, right, 0);

	from[0] += right[0] * 32;
	from[1] += right[1] * 32;
	from[2] += right[2] * 16;

	to[0] += right[0] * 32;
	to[1] += right[1] * 32;
	to[2] += right[2] * 32;

	JP_Trace(&tr, from, playerMins, playerMaxs, to, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

	if (tr.fraction == 1)
	{
		return STRAFEAROUND_RIGHT;
	}

	from[0] -= right[0] * 64;
	from[1] -= right[1] * 64;
	from[2] -= right[2] * 64;

	to[0] -= right[0] * 64;
	to[1] -= right[1] * 64;
	to[2] -= right[2] * 64;

	JP_Trace(&tr, from, playerMins, playerMaxs, to, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

	if (tr.fraction == 1)
	{
		return STRAFEAROUND_LEFT;
	}

	return 0;
}
#endif

//Similar to the trace check, but we want to trace to see
//if there's anything we can jump over.
int BotTrace_Jump(bot_state_t* bs, vec3_t traceto)
{
	vec3_t mins, maxs, forward, start, end, checkPos;
	trace_t tr;
	float dist;

	// ========================================
	// STRICT COOLDOWN CHECK - PREVENT SPAM
	// ========================================
	if (bs->jumpTime > level.time + 500) { // Extra 500ms buffer
		return 0;
	}

	// Calculate direction to target
	VectorSubtract(traceto, bs->origin, forward);
	VectorNormalize(forward);
	dist = VectorLength(forward);

	// Set bot bounding box
	VectorSet(mins, -15, -15, -18);
	VectorSet(maxs, 15, 15, 32);

	// ========================================
	// CONSERVATIVE OBSTACLE DETECTION - REDUCED SPAM
	// ========================================

	// Only check for obstacles if we're actually moving toward something
	if (dist < 64) {
		return 0; // Too close to be jumping
	}

	// 1. Check for SIGNIFICANT obstacles only
	VectorMA(bs->origin, 64, forward, end);
	JP_Trace(&tr, bs->origin, mins, maxs, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

	if (tr.fraction < 0.5) { // MAJOR obstacle only (was 0.8)
		// Check if jumping would help
		VectorCopy(bs->origin, start);
		start[2] += 32; // Higher jump height

		VectorCopy(end, checkPos);
		checkPos[2] += 32;

		JP_Trace(&tr, start, mins, maxs, checkPos, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

		if (tr.fraction > 0.8) { // Much clearer path needed (was 0.6)
			// Don't jump over enemy in melee range
			if (bs->currentEnemy && bs->currentEnemy->s.number == tr.entityNum &&
				bs->frame_Enemy_Len < 128) {
				return 0;
			}

			// Set longer cooldown to prevent spam
			bs->jumpTime = level.time + 2000; // 2 second cooldown
			return 1; // Jump is beneficial
		}
	}

	// 2. Check for DEEP pits only (removed moderate pits)
	VectorMA(bs->origin, 128, forward, end);
	end[2] -= 96; // Only deep pits

	JP_Trace(&tr, bs->origin, mins, maxs, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

	if (tr.fraction == 1.0) { // Deep pit detected
		// Check if we can make the jump
		VectorCopy(bs->origin, start);
		start[2] += 32;

		VectorMA(bs->origin, 128, forward, checkPos);
		checkPos[2] += 32;

		JP_Trace(&tr, start, mins, maxs, checkPos, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

		if (tr.fraction > 0.7) { // Can definitely make the jump
			bs->jumpTime = level.time + 2000; // 2 second cooldown
			return 1;
		}
	}

	// 3. FENCE/WALL check - ONLY in combat with visible enemy
	if (bs->currentEnemy && bs->frame_Enemy_Vis && dist < 512) {
		VectorMA(bs->origin, 48, forward, end);
		JP_Trace(&tr, bs->origin, mins, maxs, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

		if (tr.fraction < 0.7) { // Fence/low wall detected
			// Check if this is jumpable
			VectorCopy(bs->origin, start);
			start[2] += 40; // Higher jump for fences

			VectorMA(bs->origin, 80, forward, checkPos);
			checkPos[2] += 40;

			JP_Trace(&tr, start, mins, maxs, checkPos, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

			if (tr.fraction > 0.8) { // Can clear fence/wall
				bs->jumpTime = level.time + 1500; // 1.5 second cooldown
				return 1;
			}
		}
	}

	return 0; // No jump needed
} // Niksata Edit

//And yet another check to duck under any obstacles.
int BotTrace_Duck(bot_state_t* bs, vec3_t traceto)
{
	vec3_t mins, maxs, a, fwd, traceto_mod, tracefrom_mod;
	trace_t tr;

	VectorSubtract(traceto, bs->origin, a);
	vectoangles(a, a);

	AngleVectors(a, fwd, NULL, NULL);

	traceto_mod[0] = bs->origin[0] + fwd[0] * 4;
	traceto_mod[1] = bs->origin[1] + fwd[1] * 4;
	traceto_mod[2] = bs->origin[2] + fwd[2] * 4;

	mins[0] = -15;
	mins[1] = -15;
	mins[2] = -23;
	maxs[0] = 15;
	maxs[1] = 15;
	maxs[2] = 8;

	JP_Trace(&tr, bs->origin, mins, maxs, traceto_mod, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

	if (tr.fraction != 1)
	{
		return 0;
	}

	VectorCopy(bs->origin, tracefrom_mod);

	tracefrom_mod[2] += 31;
	traceto_mod[2] += 31;

	mins[0] = -15;
	mins[1] = -15;
	mins[2] = 0;
	maxs[0] = 15;
	maxs[1] = 15;
	maxs[2] = 32;

	JP_Trace(&tr, tracefrom_mod, mins, maxs, traceto_mod, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

	if (tr.fraction != 1)
	{
		// ==========================================================
		// LEDGE/WALL PROXIMITY DETECTION
		// ==========================================================

		// Check 1: Are we near a ledge/edge?
		vec3_t ledgeCheckPoints[4];
		trace_t ledgeTr[4];
		int nearLedge = 0;
		vec3_t right, up;

		AngleVectors(bs->viewangles, fwd, right, up);

		// Check in 4 directions around the bot for ledges
		for (int i = 0; i < 4; i++)
		{
			VectorCopy(bs->origin, ledgeCheckPoints[i]);

			switch (i)
			{
			case 0: // Forward
				VectorMA(ledgeCheckPoints[i], 32, fwd, ledgeCheckPoints[i]);
				break;
			case 1: // Right
				VectorMA(ledgeCheckPoints[i], 32, right, ledgeCheckPoints[i]);
				break;
			case 2: // Back
				VectorScale(fwd, -1, ledgeCheckPoints[i]);
				VectorMA(bs->origin, 32, ledgeCheckPoints[i], ledgeCheckPoints[i]);
				break;
			case 3: // Left
				VectorScale(right, -1, ledgeCheckPoints[i]);
				VectorMA(bs->origin, 32, ledgeCheckPoints[i], ledgeCheckPoints[i]);
				break;
			}

			// Check for ledge at each point
			ledgeCheckPoints[i][2] -= 64;
			JP_Trace(&ledgeTr[i], bs->origin, NULL, NULL, ledgeCheckPoints[i], bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

			// If trace goes far (no ground), we're near a ledge
			if (ledgeTr[i].fraction > 0.8)
			{
				nearLedge++;
			}
		}

		// If we're near ledges in multiple directions, don't duck
		if (nearLedge >= 2)
		{
			return 0; // Near ledge - no ducking
		}

		// Check 2: Are we near walls?
		vec3_t wallCheckPoints[4];
		trace_t wallTr[4];
		int nearWall = 0;

		for (int i = 0; i < 4; i++)
		{
			VectorCopy(tracefrom_mod, wallCheckPoints[i]);

			switch (i)
			{
			case 0: // Forward
				VectorMA(wallCheckPoints[i], 24, fwd, wallCheckPoints[i]);
				break;
			case 1: // Right
				VectorMA(wallCheckPoints[i], 24, right, wallCheckPoints[i]);
				break;
			case 2: // Back
				VectorScale(fwd, -1, wallCheckPoints[i]);
				VectorMA(tracefrom_mod, 24, wallCheckPoints[i], wallCheckPoints[i]);
				break;
			case 3: // Left
				VectorScale(right, -1, wallCheckPoints[i]);
				VectorMA(tracefrom_mod, 24, wallCheckPoints[i], wallCheckPoints[i]);
				break;
			}

			JP_Trace(&wallTr[i], tracefrom_mod, mins, maxs, wallCheckPoints[i], bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

			// If we hit a wall close by
			if (wallTr[i].fraction < 0.5)
			{
				nearWall++;
			}
		}

		// If we're near walls, be more cautious about ducking
		if (nearWall >= 1)
		{
			// Additional check: Is this wall duckable?
			vec3_t wallHeightCheck;
			trace_t wallHeightTr;

			VectorCopy(tracefrom_mod, wallHeightCheck);
			wallHeightCheck[2] += 48; // Check if there's clearance above

			JP_Trace(&wallHeightTr, tracefrom_mod, mins, maxs, wallHeightCheck, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

			// If no clearance above wall, don't duck (might get stuck)
			if (wallHeightTr.fraction < 0.3)
			{
				return 0; // Near wall with no overhead clearance
			}
		}

		// Check 3: High altitude check (from previous fix)
		vec3_t belowCheck;
		trace_t belowTr;
		float groundDistance;

		VectorCopy(bs->origin, belowCheck);
		belowCheck[2] -= 256;
		JP_Trace(&belowTr, bs->origin, NULL, NULL, belowCheck, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);
		groundDistance = bs->origin[2] - belowTr.endpos[2];

		// Don't duck if we're very high above ground
		if (groundDistance > 96)
		{
			return 0; // High altitude - no ducking
		}

		// Check 4: Final verification - Is this REALLY an overhead obstacle?
		vec3_t aboveCheck, forwardCheck;
		trace_t aboveTr, forwardTr;

		// Check directly above
		VectorCopy(tracefrom_mod, aboveCheck);
		aboveCheck[2] += 48;
		JP_Trace(&aboveTr, tracefrom_mod, mins, maxs, aboveCheck, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

		// Check forward and up
		VectorCopy(tracefrom_mod, forwardCheck);
		VectorMA(forwardCheck, 32, fwd, forwardCheck);
		forwardCheck[2] += 24;
		JP_Trace(&forwardTr, tracefrom_mod, mins, maxs, forwardCheck, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

		// Only duck if there's a consistent overhead obstacle
		if (aboveTr.fraction < 0.7 && forwardTr.fraction < 0.6)
		{
			return 1; // Genuine overhead obstacle confirmed
		}

		return 0; // Failed all checks - don't duck
	}

	return 0;
} // Niksata Edit

//check of the potential enemy is a valid one
int PassStandardEnemyChecks(bot_state_t* bs, gentity_t* en)
{
	if (!bs || !en)
	{ //shouldn't happen
		return 0;
	}

	if (!en->client)
	{ //not a client, don't care about him
		return 0;
	}

	if (en->client->sess.raceMode)
		return 0;

	if (en->client->ps.pm_type == PM_NOCLIP)
		return 0;

	if ((g_newBotAITarget.integer < -1) && (en->r.svFlags & SVF_BOT))
		return 0;

	if (en->health < 1)
	{ //he's already dead
		return 0;
	}

	if (!en->takedamage)
	{ //a client that can't take damage?
		return 0;
	}

	if (bs->doingFallback &&
		(gLevelFlags & LEVELFLAG_IGNOREINFALLBACK))
	{ //we screwed up in our nav routines somewhere and we've reverted to a fallback state to
		//try to get back on the trail. If the level specifies to ignore enemies in this state,
		//then ignore them.
		return 0;
	}

	if (en->client->ps.pm_type == PM_INTERMISSION ||
		en->client->ps.pm_type == PM_SPECTATOR ||
		en->client->sess.sessionTeam == TEAM_SPECTATOR)
	{ //don't attack spectators
		return 0;
	}

	if (!en->client->pers.connected)
	{ //a "zombie" client?
		return 0;
	}

	if (!en->s.solid)
	{ //shouldn't happen
		return 0;
	}

	if (bs->client == en->s.number)
	{ //don't attack yourself
		return 0;
	}

	if (OnSameTeam(&g_entities[bs->client], en))
	{ //don't attack teammates
		return 0;
	}

	if (BotMindTricked(bs->client, en->s.number))
	{
		if (bs->currentEnemy && bs->currentEnemy->s.number == en->s.number)
		{ //if mindtricked by this enemy, then be less "aware" of them, even though
			//we know they're there.
			vec3_t vs;
			float vLen = 0;

			VectorSubtract(bs->origin, en->client->ps.origin, vs);
			vLen = VectorLength(vs);

			if (vLen > 64 /*&& (level.time - en->client->dangerTime) > 150*/)
			{
				return 0;
			}
		}
	}

	if (en->client->ps.duelInProgress && en->client->ps.duelIndex != bs->client)
	{ //don't attack duelists unless you're dueling them
		return 0;
	}

	if (bs->cur_ps.duelInProgress && en->s.number != bs->cur_ps.duelIndex)
	{ //ditto, the other way around
		return 0;
	}

	if (level.gametype == GT_JEDIMASTER && !en->client->ps.isJediMaster && !bs->cur_ps.isJediMaster)
	{ //rules for attacking non-JM in JM mode
		vec3_t vs;
		float vLen = 0;

		if (!g_friendlyFire.value)
		{ //can't harm non-JM in JM mode if FF is off
			return 0;
		}

		VectorSubtract(bs->origin, en->client->ps.origin, vs);
		vLen = VectorLength(vs);

		if (vLen > 350)
		{
			return 0;
		}
	}

	return 1;
}

//Notifies the bot that he has taken damage from "attacker".
void BotDamageNotification(gclient_t* bot, gentity_t* attacker)
{
	bot_state_t* bs;
	bot_state_t* bs_a;
	int i;

	if (!bot || !attacker || !attacker->client)
	{
		return;
	}

	if (bot->ps.clientNum >= MAX_CLIENTS)
	{ //an NPC.. do nothing for them.
		return;
	}

	if (attacker->s.number >= MAX_CLIENTS)
	{ //if attacker is an npc also don't care I suppose.
		return;
	}

	bs_a = botstates[attacker->s.number];

	if (bs_a)
	{ //if the client attacking us is a bot as well
		bs_a->lastAttacked = &g_entities[bot->ps.clientNum];
		i = 0;

		while (i < MAX_CLIENTS)
		{
			if (botstates[i] &&
				i != bs_a->client &&
				botstates[i]->lastAttacked == &g_entities[bot->ps.clientNum])
			{
				botstates[i]->lastAttacked = NULL;
			}

			i++;
		}
	}
	else //got attacked by a real client, so no one gets rights to lastAttacked
	{
		i = 0;

		while (i < MAX_CLIENTS)
		{
			if (botstates[i] &&
				botstates[i]->lastAttacked == &g_entities[bot->ps.clientNum])
			{
				botstates[i]->lastAttacked = NULL;
			}

			i++;
		}
	}

	bs = botstates[bot->ps.clientNum];

	if (!bs)
	{
		return;
	}

	bs->lastHurt = attacker;

	if (bs->currentEnemy)
	{ //we don't care about the guy attacking us if we have an enemy already
		return;
	}

	if (!PassStandardEnemyChecks(bs, attacker))
	{ //the person that hurt us is not a valid enemy
		return;
	}

	if (PassLovedOneCheck(bs, attacker))
	{ //the person that hurt us is the one we love!
		bs->currentEnemy = attacker;
		bs->enemySeenTime = level.time + ENEMY_FORGET_MS;
	}
}

//perform cheap "hearing" checks based on the event catching
//system
int BotCanHear(bot_state_t* bs, gentity_t* en, float endist)
{
	float minlen;

	if (!en || !en->client)
	{
		return 0;
	}

	if (en && en->client && en->client->ps.otherSoundTime > level.time)
	{ //they made a noise in recent time
		minlen = en->client->ps.otherSoundLen;
		goto checkStep;
	}

	if (en && en->client && en->client->ps.footstepTime > level.time)
	{ //they made a footstep
		minlen = 256;
		goto checkStep;
	}

	if (gBotEventTracker[en->s.number].eventTime < level.time)
	{ //no recent events to check
		return 0;
	}

	switch (gBotEventTracker[en->s.number].events[gBotEventTracker[en->s.number].eventSequence & (MAX_PS_EVENTS - 1)])
	{ //did the last event contain a sound?
	case EV_GLOBAL_SOUND:
		minlen = 256;
		break;
	case EV_FIRE_WEAPON:
	case EV_ALT_FIRE:
	case EV_SABER_ATTACK:
		minlen = 512;
		break;
	case EV_STEP_4:
	case EV_STEP_8:
	case EV_STEP_12:
	case EV_STEP_16:
	case EV_FOOTSTEP:
	case EV_FOOTSTEP_METAL:
	case EV_FOOTWADE:
		minlen = 256;
		break;
	case EV_JUMP:
	case EV_ROLL:
		minlen = 256;
		break;
	default:
		minlen = 999999;
		break;
	}
checkStep:
	if (BotMindTricked(bs->client, en->s.number))
	{ //if mindtricked by this person, cut down on the minlen so they can't "hear" as well
		minlen /= 4;
	}

	if (endist <= minlen)
	{ //we heard it
		return 1;
	}

	return 0;
}

//check for new events
void UpdateEventTracker(void)
{
	int i;

	i = 0;

	while (i < MAX_CLIENTS)
	{
		if (gBotEventTracker[i].eventSequence != level.clients[i].ps.eventSequence)
		{ //updated event
			gBotEventTracker[i].eventSequence = level.clients[i].ps.eventSequence;
			gBotEventTracker[i].events[0] = level.clients[i].ps.events[0];
			gBotEventTracker[i].events[1] = level.clients[i].ps.events[1];
			gBotEventTracker[i].eventTime = level.time + 0.5;
		}

		i++;
	}
}

//check if said angles are within our fov
int InFieldOfVision(vec3_t viewangles, float fov, vec3_t angles)
{
	int i;
	float diff, angle;

	for (i = 0; i < 2; i++)
	{
		angle = AngleMod(viewangles[i]);
		angles[i] = AngleMod(angles[i]);
		diff = angles[i] - angle;
		if (angles[i] > angle)
		{
			if (diff > 180.0)
			{
				diff -= 360.0;
			}
		}
		else
		{
			if (diff < -180.0)
			{
				diff += 360.0;
			}
		}
		if (diff > 0)
		{
			if (diff > fov * 0.5)
			{
				return 0;
			}
		}
		else
		{
			if (diff < -fov * 0.5)
			{
				return 0;
			}
		}
	}
	return 1;
}

//We cannot hurt the ones we love. Unless of course this
//function says we can.
int PassLovedOneCheck(bot_state_t* bs, gentity_t* ent)
{
	int i;
	bot_state_t* loved;

	if (!bs->lovednum)
	{
		return 1;
	}

	if (level.gametype == GT_DUEL || level.gametype == GT_POWERDUEL)
	{ //There is no love in 1-on-1
		return 1;
	}

	i = 0;

	if (!botstates[ent->s.number])
	{ //not a bot
		return 1;
	}

	if (!bot_attachments.integer)
	{
		return 1;
	}

	loved = botstates[ent->s.number];

	while (i < bs->lovednum)
	{
		if (strcmp(level.clients[loved->client].pers.netname, bs->loved[i].name) == 0)
		{
			if (!IsTeamplay() && bs->loved[i].level < 2)
			{ //if FFA and level of love is not greater than 1, just don't care
				return 1;
			}
			else if (IsTeamplay() && !OnSameTeam(&g_entities[bs->client], &g_entities[loved->client]) && bs->loved[i].level < 2)
			{ //is teamplay, but not on same team and level < 2
				return 1;
			}
			else
			{
				return 0;
			}
		}

		i++;
	}

	return 1;
}

qboolean G_ThereIsAMaster(void);

//standard check to find a new enemy.
int ScanForEnemies(bot_state_t* bs)
{
	vec3_t a;
	float distcheck;
	float closest;
	int bestindex;
	int i;
	float hasEnemyDist = 0;
	qboolean noAttackNonJM = qfalse;

	closest = 999999;
	i = 0;
	bestindex = -1;

	if (bs->currentEnemy)
	{ //only switch to a new enemy if he's significantly closer
		hasEnemyDist = bs->frame_Enemy_Len;
	}

	while (i <= MAX_CLIENTS)
	{
		if (i != bs->client && g_entities[i].client && !OnSameTeam(&g_entities[bs->client], &g_entities[i]) && PassStandardEnemyChecks(bs, &g_entities[i]) && BotPVSCheck(g_entities[i].client->ps.origin, bs->eye) && PassLovedOneCheck(bs, &g_entities[i]))
		{
			VectorSubtract(g_entities[i].client->ps.origin, bs->eye, a);
			distcheck = VectorLength(a);
			vectoangles(a, a);
			/*
						if (g_entities[i].client->ps.isJediMaster)
						{ //make us think the Jedi Master is close so we'll attack him above all
							distcheck = 1;
						}
			*/
			if (distcheck < closest && ((InFieldOfVision(bs->viewangles, 90, a) && !BotMindTricked(bs->client, i)) || BotCanHear(bs, &g_entities[i], distcheck)) && OrgVisible(bs->eye, g_entities[i].client->ps.origin, -1))
			{
				if (BotMindTricked(bs->client, i))
				{
					if (distcheck < 256 || (level.time - g_entities[i].client->dangerTime) < 100)
					{
						if (!hasEnemyDist || distcheck < (hasEnemyDist - 128))
						{ //if we have an enemy, only switch to closer if he is 128+ closer to avoid flipping out
							if (!noAttackNonJM)	//|| g_entities[i].client->ps.isJediMaster)
							{
								closest = distcheck;
								bestindex = i;
							}
						}
					}
				}
				else
				{
					if (!hasEnemyDist || distcheck < (hasEnemyDist - 128))
					{ //if we have an enemy, only switch to closer if he is 128+ closer to avoid flipping out
						if (!noAttackNonJM)	//|| g_entities[i].client->ps.isJediMaster)
						{
							closest = distcheck;
							bestindex = i;
						}
					}
				}
			}
		}
		i++;
	}

	return bestindex;
}

int WaitingForNow(bot_state_t* bs, vec3_t goalpos)
{ //checks if the bot is doing something along the lines of waiting for an elevator to raise up
	vec3_t xybot, xywp, a;

	if (!bs->wpCurrent)
	{
		return 0;
	}

	if ((int)goalpos[0] != (int)bs->wpCurrent->origin[0] ||
		(int)goalpos[1] != (int)bs->wpCurrent->origin[1] ||
		(int)goalpos[2] != (int)bs->wpCurrent->origin[2])
	{
		return 0;
	}

	VectorCopy(bs->origin, xybot);
	VectorCopy(bs->wpCurrent->origin, xywp);

	xybot[2] = 0;
	xywp[2] = 0;

	VectorSubtract(xybot, xywp, a);

	if (VectorLength(a) < 16 && bs->frame_Waypoint_Len > 100)
	{
		if (CheckForFunc(bs->origin, bs->client))
		{
			return 1; //we're probably standing on an elevator and riding up/down. Or at least we hope so.
		}
	}
	else if (VectorLength(a) < 64 && bs->frame_Waypoint_Len > 64 &&
		CheckForFunc(bs->origin, bs->client))
	{
		bs->noUseTime = level.time + 2000;
	}

	return 0;
}

//get an ideal distance for us to be at in relation to our opponent
//based on our weapon.
int BotGetWeaponRange(bot_state_t* bs)
{
	switch (bs->cur_ps.weapon)
	{
	case WP_STUN_BATON:
	case WP_MELEE:
		return BWEAPONRANGE_MELEE;
	case WP_SABER:
		return BWEAPONRANGE_SABER;
	case WP_BRYAR_PISTOL:
		return BWEAPONRANGE_MID;
	case WP_BLASTER:
		return BWEAPONRANGE_MID;
	case WP_DISRUPTOR:
		return BWEAPONRANGE_LONG;
	case WP_BOWCASTER:
		return BWEAPONRANGE_LONG;
	case WP_REPEATER:
		return BWEAPONRANGE_MID;
	case WP_DEMP2:
		return BWEAPONRANGE_LONG;
	case WP_FLECHETTE:
		return BWEAPONRANGE_MID;
	case WP_ROCKET_LAUNCHER:
		return BWEAPONRANGE_LONG;
	case WP_THERMAL:
		return BWEAPONRANGE_LONG;
	case WP_TRIP_MINE:
		return BWEAPONRANGE_LONG;
	case WP_DET_PACK:
		return BWEAPONRANGE_LONG;
	default:
		return BWEAPONRANGE_MID;
	}
}

//see if we want to run away from the opponent for whatever reason
int BotIsAChickenWuss(bot_state_t* bs)
{
	int bWRange;

	if (gLevelFlags & LEVELFLAG_IMUSTNTRUNAWAY)
	{ //The level says we mustn't run away!
		return 0;
	}

	if (level.gametype == GT_SINGLE_PLAYER)
	{ //"coop" (not really)
		return 0;
	}

	if (level.gametype == GT_JEDIMASTER && !bs->cur_ps.isJediMaster)
	{ //Then you may know no fear.
		//Well, unless he's strong.
		if (bs->currentEnemy && bs->currentEnemy->client &&
			bs->currentEnemy->client->ps.isJediMaster &&
			bs->currentEnemy->health > 40 &&
			bs->cur_ps.weapon < WP_ROCKET_LAUNCHER)
		{ //explosive weapons are most effective against the Jedi Master
			goto jmPass;
		}
		return 0;
	}

	if (level.gametype == GT_CTF && bs->currentEnemy && bs->currentEnemy->client)
	{
		if (bs->currentEnemy->client->ps.powerups[PW_REDFLAG] ||
			bs->currentEnemy->client->ps.powerups[PW_BLUEFLAG])
		{ //don't be afraid of flag carriers, they must die!
			return 0;
		}
	}

jmPass:
	if (bs->chickenWussCalculationTime > level.time)
	{
		return 2; //don't want to keep going between two points...
	}

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_RAGE))
	{ //don't run while raging
		return 0;
	}

	if (level.gametype == GT_JEDIMASTER && !bs->cur_ps.isJediMaster)
	{ //be frightened of the jedi master? I guess in this case.
		return 1;
	}

	bs->chickenWussCalculationTime = level.time + MAX_CHICKENWUSS_TIME;

	if (g_entities[bs->client].health < BOT_RUN_HEALTH)
	{ //we're low on health, let's get away
		return 1;
	}

	bWRange = BotGetWeaponRange(bs);

	if (bWRange == BWEAPONRANGE_MELEE || bWRange == BWEAPONRANGE_SABER)
	{
		if (bWRange != BWEAPONRANGE_SABER || !bs->saberSpecialist)
		{ //run away if we're using melee, or if we're using a saber and not a "saber specialist"
			return 1;
		}
	}

	if (bs->cur_ps.weapon == WP_BRYAR_PISTOL)
	{ //the bryar is a weak weapon, so just try to find a new one if it's what you're having to use
		return 1;
	}

	if (bs->currentEnemy && bs->currentEnemy->client &&
		bs->currentEnemy->client->ps.weapon == WP_SABER &&
		bs->frame_Enemy_Len < 512 && bs->cur_ps.weapon != WP_SABER)
	{ //if close to an enemy with a saber and not using a saber, then try to back off
		return 1;
	}

	if ((level.time - bs->cur_ps.electrifyTime) < 16000)
	{ //lightning is dangerous.
		return 1;
	}

	//didn't run, reset the timer
	bs->chickenWussCalculationTime = 0;

	return 0;
}

//look for "bad things". bad things include detpacks, thermal detonators,
//and other dangerous explodey items.
gentity_t* GetNearestBadThing(bot_state_t* bs)
{
	int i = 0;
	float glen;
	vec3_t hold;
	int bestindex = 0;
	float bestdist = 800; //if not within a radius of 800, it's no threat anyway
	int foundindex = 0;
	float factor = 0;
	gentity_t* ent;
	trace_t tr;

	while (i < level.num_entities)
	{
		ent = &g_entities[i];

		if ((ent &&
			!ent->client &&
			ent->inuse &&
			ent->damage &&
			/*(ent->s.weapon == WP_THERMAL || ent->s.weapon == WP_FLECHETTE)*/
			ent->s.weapon &&
			ent->splashDamage) ||
			(ent &&
				ent->genericValue5 == 1000 &&
				ent->inuse &&
				ent->health > 0 &&
				ent->genericValue3 != bs->client &&
				g_entities[ent->genericValue3].client && !OnSameTeam(&g_entities[bs->client], &g_entities[ent->genericValue3])))
		{ //try to escape from anything with a non-0 s.weapon and non-0 damage. This hopefully only means dangerous projectiles.
		  //Or a sentry gun if bolt_Head == 1000. This is a terrible hack, yes.
			VectorSubtract(bs->origin, ent->r.currentOrigin, hold);
			glen = VectorLength(hold);

			if (ent->s.weapon != WP_THERMAL && ent->s.weapon != WP_FLECHETTE &&
				ent->s.weapon != WP_DET_PACK && ent->s.weapon != WP_TRIP_MINE)
			{
				factor = 0.5;

				if (ent->s.weapon && glen <= 256 && bs->settings.skill > 2)
				{ //it's a projectile so push it away
					bs->doForcePush = level.time + 700;
					//trap->Print("PUSH PROJECTILE\n");
				}
			}
			else
			{
				factor = 1;
			}

			if (ent->s.weapon == WP_ROCKET_LAUNCHER &&
				(ent->r.ownerNum == bs->client ||
					(ent->r.ownerNum > 0 && ent->r.ownerNum < MAX_CLIENTS &&
						g_entities[ent->r.ownerNum].client && OnSameTeam(&g_entities[bs->client], &g_entities[ent->r.ownerNum]))))
			{ //don't be afraid of your own rockets or your teammates' rockets
				factor = 0;
			}

			if (ent->s.weapon == WP_DET_PACK &&
				(ent->r.ownerNum == bs->client ||
					(ent->r.ownerNum > 0 && ent->r.ownerNum < MAX_CLIENTS &&
						g_entities[ent->r.ownerNum].client && OnSameTeam(&g_entities[bs->client], &g_entities[ent->r.ownerNum]))))
			{ //don't be afraid of your own detpacks or your teammates' detpacks
				factor = 0;
			}

			if (ent->s.weapon == WP_TRIP_MINE &&
				(ent->r.ownerNum == bs->client ||
					(ent->r.ownerNum > 0 && ent->r.ownerNum < MAX_CLIENTS &&
						g_entities[ent->r.ownerNum].client && OnSameTeam(&g_entities[bs->client], &g_entities[ent->r.ownerNum]))))
			{ //don't be afraid of your own trip mines or your teammates' trip mines
				factor = 0;
			}

			if (ent->s.weapon == WP_THERMAL &&
				(ent->r.ownerNum == bs->client ||
					(ent->r.ownerNum > 0 && ent->r.ownerNum < MAX_CLIENTS &&
						g_entities[ent->r.ownerNum].client && OnSameTeam(&g_entities[bs->client], &g_entities[ent->r.ownerNum]))))
			{ //don't be afraid of your own thermals or your teammates' thermals
				factor = 0;
			}

			if (glen < bestdist * factor && BotPVSCheck(bs->origin, ent->s.pos.trBase))
			{
				JP_Trace(&tr, bs->origin, NULL, NULL, ent->s.pos.trBase, bs->client, MASK_SOLID, qfalse, 0, 0);

				if (tr.fraction == 1 || tr.entityNum == ent->s.number)
				{
					bestindex = i;
					bestdist = glen;
					foundindex = 1;
				}
			}
		}

		if (ent && !ent->client && ent->inuse && ent->damage && ent->s.weapon && ent->r.ownerNum < MAX_CLIENTS && ent->r.ownerNum >= 0)
		{ //if we're in danger of a projectile belonging to someone and don't have an enemy, set the enemy to them
			gentity_t* projOwner = &g_entities[ent->r.ownerNum];

			if (projOwner && projOwner->inuse && projOwner->client)
			{
				if (!bs->currentEnemy)
				{
					if (PassStandardEnemyChecks(bs, projOwner))
					{
						if (PassLovedOneCheck(bs, projOwner))
						{
							VectorSubtract(bs->origin, ent->r.currentOrigin, hold);
							glen = VectorLength(hold);

							if (glen < 512)
							{
								bs->currentEnemy = projOwner;
								bs->enemySeenTime = level.time + ENEMY_FORGET_MS;
							}
						}
					}
				}
			}
		}

		i++;
	}

	if (foundindex)
	{
		bs->dontGoBack = level.time + 1500;
		return &g_entities[bestindex];
	}
	else
	{
		return NULL;
	}
}

//Keep our CTF priorities on defending our team's flag
int BotDefendFlag(bot_state_t* bs)
{
	wpobject_t* flagPoint;
	vec3_t a;

	if (level.clients[bs->client].sess.sessionTeam == TEAM_RED)
	{
		flagPoint = flagRed;
	}
	else if (level.clients[bs->client].sess.sessionTeam == TEAM_BLUE)
	{
		flagPoint = flagBlue;
	}
	else
	{
		return 0;
	}

	if (!flagPoint)
	{
		return 0;
	}

	VectorSubtract(bs->origin, flagPoint->origin, a);

	if (VectorLength(a) > BASE_GUARD_DISTANCE)
	{
		bs->wpDestination = flagPoint;
	}

	return 1;
}

//Keep our CTF priorities on getting the other team's flag
int BotGetEnemyFlag(bot_state_t* bs)
{
	wpobject_t* flagPoint;
	vec3_t a;

	if (level.clients[bs->client].sess.sessionTeam == TEAM_RED)
	{
		flagPoint = flagBlue;
	}
	else if (level.clients[bs->client].sess.sessionTeam == TEAM_BLUE)
	{
		flagPoint = flagRed;
	}
	else
	{
		return 0;
	}

	if (!flagPoint)
	{
		return 0;
	}

	VectorSubtract(bs->origin, flagPoint->origin, a);

	if (VectorLength(a) > BASE_GETENEMYFLAG_DISTANCE)
	{
		bs->wpDestination = flagPoint;
	}

	return 1;
}

//Our team's flag is gone, so try to get it back
int BotGetFlagBack(bot_state_t* bs)
{
	int i = 0;
	int myFlag = 0;
	int foundCarrier = 0;
	int tempInt = 0;
	gentity_t* ent = NULL;
	vec3_t usethisvec;

	if (level.clients[bs->client].sess.sessionTeam == TEAM_RED)
	{
		myFlag = PW_REDFLAG;
	}
	else
	{
		myFlag = PW_BLUEFLAG;
	}

	while (i < MAX_CLIENTS)
	{
		ent = &g_entities[i];

		if (ent && ent->client && ent->client->ps.powerups[myFlag] && !OnSameTeam(&g_entities[bs->client], ent))
		{
			foundCarrier = 1;
			break;
		}

		i++;
	}

	if (!foundCarrier)
	{
		return 0;
	}

	if (!ent)
	{
		return 0;
	}

	if (bs->wpDestSwitchTime < level.time)
	{
		if (ent->client)
		{
			VectorCopy(ent->client->ps.origin, usethisvec);
		}
		else
		{
			VectorCopy(ent->s.origin, usethisvec);
		}

		tempInt = GetNearestVisibleWP(usethisvec, 0);

		if (tempInt != -1 && TotalTrailDistance(bs->wpCurrent->index, tempInt, bs) != -1)
		{
			bs->wpDestination = gWPArray[tempInt];
			bs->wpDestSwitchTime = level.time + Q_irand(1000, 5000);
		}
	}

	return 1;
}

//Someone else on our team has the enemy flag, so try to get
//to their assistance
int BotGuardFlagCarrier(bot_state_t* bs)
{
	int i = 0;
	int enemyFlag = 0;
	int foundCarrier = 0;
	int tempInt = 0;
	gentity_t* ent = NULL;
	vec3_t usethisvec;

	if (level.clients[bs->client].sess.sessionTeam == TEAM_RED)
	{
		enemyFlag = PW_BLUEFLAG;
	}
	else
	{
		enemyFlag = PW_REDFLAG;
	}

	while (i < MAX_CLIENTS)
	{
		ent = &g_entities[i];

		if (ent && ent->client && ent->client->ps.powerups[enemyFlag] && OnSameTeam(&g_entities[bs->client], ent))
		{
			foundCarrier = 1;
			break;
		}

		i++;
	}

	if (!foundCarrier)
	{
		return 0;
	}

	if (!ent)
	{
		return 0;
	}

	if (bs->wpDestSwitchTime < level.time)
	{
		if (ent->client)
		{
			VectorCopy(ent->client->ps.origin, usethisvec);
		}
		else
		{
			VectorCopy(ent->s.origin, usethisvec);
		}

		tempInt = GetNearestVisibleWP(usethisvec, 0);

		if (tempInt != -1 && TotalTrailDistance(bs->wpCurrent->index, tempInt, bs) != -1)
		{
			bs->wpDestination = gWPArray[tempInt];
			bs->wpDestSwitchTime = level.time + Q_irand(1000, 5000);
		}
	}

	return 1;
}

//We have the flag, let's get it home.
int BotGetFlagHome(bot_state_t* bs)
{
	wpobject_t* flagPoint;
	vec3_t a;

	if (level.clients[bs->client].sess.sessionTeam == TEAM_RED)
	{
		flagPoint = flagRed;
	}
	else if (level.clients[bs->client].sess.sessionTeam == TEAM_BLUE)
	{
		flagPoint = flagBlue;
	}
	else
	{
		return 0;
	}

	if (!flagPoint)
	{
		return 0;
	}

	VectorSubtract(bs->origin, flagPoint->origin, a);

	if (VectorLength(a) > BASE_FLAGWAIT_DISTANCE)
	{
		bs->wpDestination = flagPoint;
	}

	return 1;
}

void GetNewFlagPoint(wpobject_t* wp, gentity_t* flagEnt, int team)
{ //get the nearest possible waypoint to the flag since it's not in its original position
	int i = 0;
	vec3_t a, mins, maxs;
	float bestdist;
	float testdist;
	int bestindex = 0;
	int foundindex = 0;
	trace_t tr;

	mins[0] = -15;
	mins[1] = -15;
	mins[2] = -5;
	maxs[0] = 15;
	maxs[1] = 15;
	maxs[2] = 5;

	VectorSubtract(wp->origin, flagEnt->s.pos.trBase, a);

	bestdist = VectorLength(a);

	if (bestdist <= WP_KEEP_FLAG_DIST)
	{
		JP_Trace(&tr, wp->origin, mins, maxs, flagEnt->s.pos.trBase, flagEnt->s.number, MASK_SOLID, qfalse, 0, 0);

		if (tr.fraction == 1)
		{ //this point is good
			return;
		}
	}

	while (i < gWPNum)
	{
		VectorSubtract(gWPArray[i]->origin, flagEnt->s.pos.trBase, a);
		testdist = VectorLength(a);

		if (testdist < bestdist)
		{
			JP_Trace(&tr, gWPArray[i]->origin, mins, maxs, flagEnt->s.pos.trBase, flagEnt->s.number, MASK_SOLID, qfalse, 0, 0);

			if (tr.fraction == 1)
			{
				foundindex = 1;
				bestindex = i;
				bestdist = testdist;
			}
		}

		i++;
	}

	if (foundindex)
	{
		if (team == TEAM_RED)
		{
			flagRed = gWPArray[bestindex];
		}
		else
		{
			flagBlue = gWPArray[bestindex];
		}
	}
}

//See if our CTF state should take priority in our nav routines
int CTFTakesPriority(bot_state_t* bs)
{
	gentity_t* ent = NULL;
	int enemyFlag = 0;
	int myFlag = 0;
	int enemyHasOurFlag = 0;
	//int weHaveEnemyFlag = 0;
	int numOnMyTeam = 0;
	//int numOnEnemyTeam = 0;
	int numAttackers = 0;
	//int numDefenders = 0;
	int i = 0;
	int idleWP;
	int dosw = 0;
	wpobject_t* dest_sw = NULL;
#ifdef BOT_CTF_DEBUG
	vec3_t t;

	trap->Print("CTFSTATE: %s\n", ctfStateNames[bs->ctfState]);
#endif

	if (level.gametype != GT_CTF && level.gametype != GT_CTY)
	{
		return 0;
	}

	if (bs->cur_ps.weapon == WP_BRYAR_PISTOL &&
		(level.time - bs->lastDeadTime) < BOT_MAX_WEAPON_GATHER_TIME)
	{ //get the nearest weapon laying around base before heading off for battle
		idleWP = GetBestIdleGoal(bs);

		if (idleWP != -1 && gWPArray[idleWP] && gWPArray[idleWP]->inuse)
		{
			if (bs->wpDestSwitchTime < level.time)
			{
				bs->wpDestination = gWPArray[idleWP];
			}
			return 1;
		}
	}
	else if (bs->cur_ps.weapon == WP_BRYAR_PISTOL &&
		(level.time - bs->lastDeadTime) < BOT_MAX_WEAPON_CHASE_CTF &&
		bs->wpDestination && bs->wpDestination->weight)
	{
		dest_sw = bs->wpDestination;
		dosw = 1;
	}

	if (level.clients[bs->client].sess.sessionTeam == TEAM_RED)
	{
		myFlag = PW_REDFLAG;
	}
	else
	{
		myFlag = PW_BLUEFLAG;
	}

	if (level.clients[bs->client].sess.sessionTeam == TEAM_RED)
	{
		enemyFlag = PW_BLUEFLAG;
	}
	else
	{
		enemyFlag = PW_REDFLAG;
	}

	if (!flagRed || !flagBlue ||
		!flagRed->inuse || !flagBlue->inuse ||
		!eFlagRed || !eFlagBlue)
	{
		return 0;
	}

#ifdef BOT_CTF_DEBUG
	VectorCopy(flagRed->origin, t);
	t[2] += 128;
	G_TestLine(flagRed->origin, t, 0x0000ff, 500);

	VectorCopy(flagBlue->origin, t);
	t[2] += 128;
	G_TestLine(flagBlue->origin, t, 0x0000ff, 500);
#endif

	if (droppedRedFlag && (droppedRedFlag->flags & FL_DROPPED_ITEM))
	{
		GetNewFlagPoint(flagRed, droppedRedFlag, TEAM_RED);
	}
	else
	{
		flagRed = oFlagRed;
	}

	if (droppedBlueFlag && (droppedBlueFlag->flags & FL_DROPPED_ITEM))
	{
		GetNewFlagPoint(flagBlue, droppedBlueFlag, TEAM_BLUE);
	}
	else
	{
		flagBlue = oFlagBlue;
	}

	if (!bs->ctfState)
	{
		return 0;
	}

	i = 0;

	while (i < MAX_CLIENTS)
	{
		ent = &g_entities[i];

		if (ent && ent->client)
		{
			/*if (ent->client->ps.powerups[enemyFlag] && OnSameTeam(&g_entities[bs->client], ent))
			{
				weHaveEnemyFlag = 1;
			}
			else */if (ent->client->ps.powerups[myFlag] && !OnSameTeam(&g_entities[bs->client], ent))
			{
				enemyHasOurFlag = 1;
			}

			if (OnSameTeam(&g_entities[bs->client], ent))
			{
				numOnMyTeam++;
			}
			else
			{
				//numOnEnemyTeam++;
			}

			if (botstates[ent->s.number])
			{
				if (botstates[ent->s.number]->ctfState == CTFSTATE_ATTACKER ||
					botstates[ent->s.number]->ctfState == CTFSTATE_RETRIEVAL)
				{
					numAttackers++;
				}
				else
				{
					//numDefenders++;
				}
			}
			else
			{ //assume real players to be attackers in our logic
				numAttackers++;
			}
		}
		i++;
	}

	if (bs->cur_ps.powerups[enemyFlag])
	{
		if ((numOnMyTeam < 2 || !numAttackers) && enemyHasOurFlag)
		{
			bs->ctfState = CTFSTATE_RETRIEVAL;
		}
		else
		{
			bs->ctfState = CTFSTATE_GETFLAGHOME;
		}
	}
	else if (bs->ctfState == CTFSTATE_GETFLAGHOME)
	{
		bs->ctfState = 0;
	}

	if (bs->state_Forced)
	{
		bs->ctfState = bs->state_Forced;
	}

	if (bs->ctfState == CTFSTATE_DEFENDER)
	{
		if (BotDefendFlag(bs))
		{
			goto success;
		}
	}

	if (bs->ctfState == CTFSTATE_ATTACKER)
	{
		if (BotGetEnemyFlag(bs))
		{
			goto success;
		}
	}

	if (bs->ctfState == CTFSTATE_RETRIEVAL)
	{
		if (BotGetFlagBack(bs))
		{
			goto success;
		}
		else
		{ //can't find anyone on another team being a carrier, so ignore this priority
			bs->ctfState = 0;
		}
	}

	if (bs->ctfState == CTFSTATE_GUARDCARRIER)
	{
		if (BotGuardFlagCarrier(bs))
		{
			goto success;
		}
		else
		{ //can't find anyone on our team being a carrier, so ignore this priority
			bs->ctfState = 0;
		}
	}

	if (bs->ctfState == CTFSTATE_GETFLAGHOME)
	{
		if (BotGetFlagHome(bs))
		{
			goto success;
		}
	}

	return 0;

success:
	if (dosw)
	{ //allow ctf code to run, but if after a particular item then keep going after it
		bs->wpDestination = dest_sw;
	}

	return 1;
}

int EntityVisibleBox(vec3_t org1, vec3_t mins, vec3_t maxs, vec3_t org2, int ignore, int ignore2)
{
	trace_t tr;

	JP_Trace(&tr, org1, mins, maxs, org2, ignore, MASK_SOLID, qfalse, 0, 0);

	if (tr.fraction == 1 && !tr.startsolid && !tr.allsolid)
	{
		return 1;
	}
	else if (tr.entityNum != ENTITYNUM_NONE && tr.entityNum == ignore2)
	{
		return 1;
	}

	return 0;
}

//Get the closest objective for siege and go after it
int Siege_TargetClosestObjective(bot_state_t* bs, int flag)
{
	int i = 0;
	int bestindex = -1;
	float testdistance = 0;
	float bestdistance = 999999999.9f;
	gentity_t* goalent;
	vec3_t a, dif;
	vec3_t mins, maxs;

	mins[0] = -1;
	mins[1] = -1;
	mins[2] = -1;

	maxs[0] = 1;
	maxs[1] = 1;
	maxs[2] = 1;

	if (bs->wpDestination && (bs->wpDestination->flags & flag) && bs->wpDestination->associated_entity != ENTITYNUM_NONE &&
		g_entities[bs->wpDestination->associated_entity].inuse && g_entities[bs->wpDestination->associated_entity].use)
	{
		goto hasPoint;
	}

	while (i < gWPNum)
	{
		if (gWPArray[i] && gWPArray[i]->inuse && (gWPArray[i]->flags & flag) && gWPArray[i]->associated_entity != ENTITYNUM_NONE &&
			g_entities[gWPArray[i]->associated_entity].inuse && g_entities[gWPArray[i]->associated_entity].use)
		{
			VectorSubtract(gWPArray[i]->origin, bs->origin, a);
			testdistance = VectorLength(a);

			if (testdistance < bestdistance)
			{
				bestdistance = testdistance;
				bestindex = i;
			}
		}

		i++;
	}

	if (bestindex != -1)
	{
		bs->wpDestination = gWPArray[bestindex];
	}
	else
	{
		return 0;
	}
hasPoint:
	goalent = &g_entities[bs->wpDestination->associated_entity];

	if (!goalent)
	{
		return 0;
	}

	VectorSubtract(bs->origin, bs->wpDestination->origin, a);

	testdistance = VectorLength(a);

	dif[0] = (goalent->r.absmax[0] + goalent->r.absmin[0]) / 2;
	dif[1] = (goalent->r.absmax[1] + goalent->r.absmin[1]) / 2;
	dif[2] = (goalent->r.absmax[2] + goalent->r.absmin[2]) / 2;
	//brush models can have tricky origins, so this is our hacky method of getting the center point

	if (goalent->takedamage && testdistance < BOT_MIN_SIEGE_GOAL_SHOOT &&
		EntityVisibleBox(bs->origin, mins, maxs, dif, bs->client, goalent->s.number))
	{
		bs->shootGoal = goalent;
		bs->touchGoal = NULL;
	}
	else if (goalent->use && testdistance < BOT_MIN_SIEGE_GOAL_TRAVEL)
	{
		bs->shootGoal = NULL;
		bs->touchGoal = goalent;
	}
	else
	{ //don't know how to handle this goal object!
		bs->shootGoal = NULL;
		bs->touchGoal = NULL;
	}

	if (BotGetWeaponRange(bs) == BWEAPONRANGE_MELEE ||
		BotGetWeaponRange(bs) == BWEAPONRANGE_SABER)
	{
		bs->shootGoal = NULL; //too risky
	}

	if (bs->touchGoal)
	{
		//trap->Print("Please, master, let me touch it!\n");
		VectorCopy(dif, bs->goalPosition);
	}

	return 1;
}

void Siege_DefendFromAttackers(bot_state_t* bs)
{ //this may be a little cheap, but the best way to find our defending point is probably
  //to just find the nearest person on the opposing team since they'll most likely
  //be on offense in this situation
	int wpClose = -1;
	int i = 0;
	float testdist = 999999;
	int bestindex = -1;
	float bestdist = 999999;
	gentity_t* ent;
	vec3_t a;

	while (i < MAX_CLIENTS)
	{
		ent = &g_entities[i];

		if (ent && ent->client && ent->client->sess.sessionTeam != g_entities[bs->client].client->sess.sessionTeam &&
			ent->health > 0 && ent->client->sess.sessionTeam != TEAM_SPECTATOR)
		{
			VectorSubtract(ent->client->ps.origin, bs->origin, a);

			testdist = VectorLength(a);

			if (testdist < bestdist)
			{
				bestindex = i;
				bestdist = testdist;
			}
		}

		i++;
	}

	if (bestindex == -1)
	{
		return;
	}

	wpClose = GetNearestVisibleWP(g_entities[bestindex].client->ps.origin, -1);

	if (wpClose != -1 && gWPArray[wpClose] && gWPArray[wpClose]->inuse)
	{
		bs->wpDestination = gWPArray[wpClose];
		bs->destinationGrabTime = level.time + 10000;
	}
}

//how many defenders on our team?
int Siege_CountDefenders(bot_state_t* bs)
{
	int i = 0;
	int num = 0;
	gentity_t* ent;
	bot_state_t* bot;

	while (i < MAX_CLIENTS)
	{
		ent = &g_entities[i];
		bot = botstates[i];

		if (ent && ent->client && bot)
		{
			if (bot->siegeState == SIEGESTATE_DEFENDER &&
				ent->client->sess.sessionTeam == g_entities[bs->client].client->sess.sessionTeam)
			{
				num++;
			}
		}

		i++;
	}

	return num;
}

//how many other players on our team?
int Siege_CountTeammates(bot_state_t* bs)
{
	int i = 0;
	int num = 0;
	gentity_t* ent;

	while (i < MAX_CLIENTS)
	{
		ent = &g_entities[i];

		if (ent && ent->client)
		{
			if (ent->client->sess.sessionTeam == g_entities[bs->client].client->sess.sessionTeam)
			{
				num++;
			}
		}

		i++;
	}

	return num;
}

//see if siege objective completion should take priority in our
//nav routines.
int SiegeTakesPriority(bot_state_t* bs)
{
	int attacker;
	//int flagForDefendableObjective;
	int flagForAttackableObjective;
	int defenders, teammates;
	int idleWP;
	wpobject_t* dest_sw = NULL;
	int dosw = 0;
	gclient_t* bcl;
	vec3_t dif;
	trace_t tr;

	if (level.gametype != GT_SIEGE)
	{
		return 0;
	}

	bcl = g_entities[bs->client].client;

	if (!bcl)
	{
		return 0;
	}

	if (bs->cur_ps.weapon == WP_BRYAR_PISTOL &&
		(level.time - bs->lastDeadTime) < BOT_MAX_WEAPON_GATHER_TIME)
	{ //get the nearest weapon laying around base before heading off for battle
		idleWP = GetBestIdleGoal(bs);

		if (idleWP != -1 && gWPArray[idleWP] && gWPArray[idleWP]->inuse)
		{
			if (bs->wpDestSwitchTime < level.time)
			{
				bs->wpDestination = gWPArray[idleWP];
			}
			return 1;
		}
	}
	else if (bs->cur_ps.weapon == WP_BRYAR_PISTOL &&
		(level.time - bs->lastDeadTime) < BOT_MAX_WEAPON_CHASE_TIME &&
		bs->wpDestination && bs->wpDestination->weight)
	{
		dest_sw = bs->wpDestination;
		dosw = 1;
	}

	if (bcl->sess.sessionTeam == SIEGETEAM_TEAM1)
	{
		attacker = imperial_attackers;
		//flagForDefendableObjective = WPFLAG_SIEGE_REBELOBJ;
		flagForAttackableObjective = WPFLAG_SIEGE_IMPERIALOBJ;
	}
	else
	{
		attacker = rebel_attackers;
		//flagForDefendableObjective = WPFLAG_SIEGE_IMPERIALOBJ;
		flagForAttackableObjective = WPFLAG_SIEGE_REBELOBJ;
	}

	if (attacker)
	{
		bs->siegeState = SIEGESTATE_ATTACKER;
	}
	else
	{
		bs->siegeState = SIEGESTATE_DEFENDER;
		defenders = Siege_CountDefenders(bs);
		teammates = Siege_CountTeammates(bs);

		if (defenders > teammates / 3 && teammates > 1)
		{ //devote around 1/4 of our team to completing our own side goals even if we're a defender.
		  //If we have no side goals we will realize that later on and join the defenders
			bs->siegeState = SIEGESTATE_ATTACKER;
		}
	}

	if (bs->state_Forced)
	{
		bs->siegeState = bs->state_Forced;
	}

	if (bs->siegeState == SIEGESTATE_ATTACKER)
	{
		if (!Siege_TargetClosestObjective(bs, flagForAttackableObjective))
		{ //looks like we have no goals other than to keep the other team from completing objectives
			Siege_DefendFromAttackers(bs);
			if (bs->shootGoal)
			{
				dif[0] = (bs->shootGoal->r.absmax[0] + bs->shootGoal->r.absmin[0]) / 2;
				dif[1] = (bs->shootGoal->r.absmax[1] + bs->shootGoal->r.absmin[1]) / 2;
				dif[2] = (bs->shootGoal->r.absmax[2] + bs->shootGoal->r.absmin[2]) / 2;

				if (!BotPVSCheck(bs->origin, dif))
				{
					bs->shootGoal = NULL;
				}
				else
				{
					JP_Trace(&tr, bs->origin, NULL, NULL, dif, bs->client, MASK_SOLID, qfalse, 0, 0);

					if (tr.fraction != 1 && tr.entityNum != bs->shootGoal->s.number)
					{
						bs->shootGoal = NULL;
					}
				}
			}
		}
	}
	else if (bs->siegeState == SIEGESTATE_DEFENDER)
	{
		Siege_DefendFromAttackers(bs);
		if (bs->shootGoal)
		{
			dif[0] = (bs->shootGoal->r.absmax[0] + bs->shootGoal->r.absmin[0]) / 2;
			dif[1] = (bs->shootGoal->r.absmax[1] + bs->shootGoal->r.absmin[1]) / 2;
			dif[2] = (bs->shootGoal->r.absmax[2] + bs->shootGoal->r.absmin[2]) / 2;

			if (!BotPVSCheck(bs->origin, dif))
			{
				bs->shootGoal = NULL;
			}
			else
			{
				JP_Trace(&tr, bs->origin, NULL, NULL, dif, bs->client, MASK_SOLID, qfalse, 0, 0);

				if (tr.fraction != 1 && tr.entityNum != bs->shootGoal->s.number)
				{
					bs->shootGoal = NULL;
				}
			}
		}
	}
	else
	{ //get busy!
		Siege_TargetClosestObjective(bs, flagForAttackableObjective);
		if (bs->shootGoal)
		{
			dif[0] = (bs->shootGoal->r.absmax[0] + bs->shootGoal->r.absmin[0]) / 2;
			dif[1] = (bs->shootGoal->r.absmax[1] + bs->shootGoal->r.absmin[1]) / 2;
			dif[2] = (bs->shootGoal->r.absmax[2] + bs->shootGoal->r.absmin[2]) / 2;

			if (!BotPVSCheck(bs->origin, dif))
			{
				bs->shootGoal = NULL;
			}
			else
			{
				JP_Trace(&tr, bs->origin, NULL, NULL, dif, bs->client, MASK_SOLID, qfalse, 0, 0);

				if (tr.fraction != 1 && tr.entityNum != bs->shootGoal->s.number)
				{
					bs->shootGoal = NULL;
				}
			}
		}
	}

	if (dosw)
	{ //allow siege objective code to run, but if after a particular item then keep going after it
		bs->wpDestination = dest_sw;
	}

	return 1;
}

//see if jedi master priorities should take priority in our nav
//routines.
int JMTakesPriority(bot_state_t* bs)
{
	int i = 0;
	int wpClose = -1;
	gentity_t* theImportantEntity = NULL;

	if (level.gametype != GT_JEDIMASTER)
	{
		return 0;
	}

	if (bs->cur_ps.isJediMaster)
	{
		return 0;
	}

	//jmState becomes the index for the one who carries the saber. If jmState is -1 then the saber is currently
	//without an owner
	bs->jmState = -1;

	while (i < MAX_CLIENTS)
	{
		if (g_entities[i].client && g_entities[i].inuse &&
			g_entities[i].client->ps.isJediMaster)
		{
			bs->jmState = i;
			break;
		}

		i++;
	}

	if (bs->jmState != -1)
	{
		theImportantEntity = &g_entities[bs->jmState];
	}
	else
	{
		theImportantEntity = gJMSaberEnt;
	}

	if (theImportantEntity && theImportantEntity->inuse && bs->destinationGrabTime < level.time)
	{
		if (theImportantEntity->client)
		{
			wpClose = GetNearestVisibleWP(theImportantEntity->client->ps.origin, theImportantEntity->s.number);
		}
		else
		{
			wpClose = GetNearestVisibleWP(theImportantEntity->r.currentOrigin, theImportantEntity->s.number);
		}

		if (wpClose != -1 && gWPArray[wpClose] && gWPArray[wpClose]->inuse)
		{
			/*
			Com_Printf("BOT GRABBED IDEAL JM LOCATION\n");
			if (bs->wpDestination != gWPArray[wpClose])
			{
				Com_Printf("IDEAL WAS NOT ALREADY IDEAL\n");

				if (!bs->wpDestination)
				{
					Com_Printf("IDEAL WAS NULL\n");
				}
			}
			*/
			bs->wpDestination = gWPArray[wpClose];
			bs->destinationGrabTime = level.time + 4000;
		}
	}

	return 1;
}

//see if we already have an item/powerup/etc. that is associated
//with this waypoint.
int BotHasAssociated(bot_state_t* bs, wpobject_t* wp)
{
	gentity_t* as;

	if (wp->associated_entity == ENTITYNUM_NONE)
	{ //make it think this is an item we have so we don't go after nothing
		return 1;
	}

	as = &g_entities[wp->associated_entity];

	if (!as || !as->item)
	{
		return 0;
	}

	if (as->item->giType == IT_WEAPON)
	{
		if (bs->cur_ps.stats[STAT_WEAPONS] & (1 << as->item->giTag))
		{
			return 1;
		}

		return 0;
	}
	else if (as->item->giType == IT_HOLDABLE)
	{
		if (bs->cur_ps.stats[STAT_HOLDABLE_ITEMS] & (1 << as->item->giTag))
		{
			return 1;
		}

		return 0;
	}
	else if (as->item->giType == IT_POWERUP)
	{
		if (bs->cur_ps.powerups[as->item->giTag])
		{
			return 1;
		}

		return 0;
	}
	else if (as->item->giType == IT_AMMO)
	{
		if (bs->cur_ps.ammo[as->item->giTag] > 10) //hack
		{
			return 1;
		}

		return 0;
	}

	return 0;
}

//we don't really have anything we want to do right now,
//let's just find the best thing to do given the current
//situation.
int GetBestIdleGoal(bot_state_t* bs)
{
	int i = 0;
	int highestweight = 0;
	int desiredindex = -1;
	int dist_to_weight = 0;
	int traildist;

	if (!bs->wpCurrent)
	{
		return -1;
	}

	if (bs->isCamper != 2)
	{
		if (bs->randomNavTime < level.time)
		{
			if (Q_irand(1, 10) < 5)
			{
				bs->randomNav = 1;
			}
			else
			{
				bs->randomNav = 0;
			}

			bs->randomNavTime = level.time + Q_irand(5000, 15000);
		}
	}

	if (bs->randomNav)
	{ //stop looking for items and/or camping on them
		return -1;
	}

	while (i < gWPNum)
	{
		if (gWPArray[i] &&
			gWPArray[i]->inuse &&
			(gWPArray[i]->flags & WPFLAG_GOALPOINT) &&
			gWPArray[i]->weight > highestweight &&
			!BotHasAssociated(bs, gWPArray[i]))
		{
			traildist = TotalTrailDistance(bs->wpCurrent->index, i, bs);

			if (traildist != -1)
			{
				dist_to_weight = (int)traildist / 10000;
				dist_to_weight = (gWPArray[i]->weight) - dist_to_weight;

				if (dist_to_weight > highestweight)
				{
					highestweight = dist_to_weight;
					desiredindex = i;
				}
			}
		}

		i++;
	}

	return desiredindex;
}

//go through the list of possible priorities for navigating
//and work out the best destination point.
void GetIdealDestination(bot_state_t* bs)
{
	int tempInt, cWPIndex, bChicken, idleWP;
	float distChange, plusLen, minusLen;
	vec3_t usethisvec, a;
	gentity_t* badthing;

#ifdef _DEBUG
	trap->Cvar_Update(&bot_nogoals);

	if (bot_nogoals.integer)
	{
		return;
	}
#endif

	if (!bs->wpCurrent)
	{
		return;
	}

	if ((level.time - bs->escapeDirTime) > 4000)
	{
		badthing = GetNearestBadThing(bs);
	}
	else
	{
		badthing = NULL;
	}

	if (badthing && badthing->inuse &&
		badthing->health > 0 && badthing->takedamage)
	{
		bs->dangerousObject = badthing;
	}
	else
	{
		bs->dangerousObject = NULL;
	}

	if (!badthing && bs->wpDestIgnoreTime > level.time)
	{
		return;
	}

	if (!badthing && bs->dontGoBack > level.time)
	{
		if (bs->wpDestination)
		{
			bs->wpStoreDest = bs->wpDestination;
		}
		bs->wpDestination = NULL;
		return;
	}
	else if (!badthing && bs->wpStoreDest)
	{ //after we finish running away, switch back to our original destination
		bs->wpDestination = bs->wpStoreDest;
		bs->wpStoreDest = NULL;
	}

	if (badthing && bs->wpCamping)
	{
		bs->wpCamping = NULL;
	}

	if (bs->wpCamping)
	{
		bs->wpDestination = bs->wpCamping;
		return;
	}

	if (!badthing && CTFTakesPriority(bs))
	{
		if (bs->ctfState)
		{
			bs->runningToEscapeThreat = 1;
		}
		return;
	}
	else if (!badthing && SiegeTakesPriority(bs))
	{
		if (bs->siegeState)
		{
			bs->runningToEscapeThreat = 1;
		}
		return;
	}
	else if (!badthing && JMTakesPriority(bs))
	{
		bs->runningToEscapeThreat = 1;
	}

	if (badthing)
	{
		bs->runningLikeASissy = level.time + 100;

		if (bs->wpDestination)
		{
			bs->wpStoreDest = bs->wpDestination;
		}
		bs->wpDestination = NULL;

		if (bs->wpDirection)
		{
			tempInt = bs->wpCurrent->index + 1;
		}
		else
		{
			tempInt = bs->wpCurrent->index - 1;
		}

		if (gWPArray[tempInt] && gWPArray[tempInt]->inuse && bs->escapeDirTime < level.time)
		{
			VectorSubtract(badthing->s.pos.trBase, bs->wpCurrent->origin, a);
			plusLen = VectorLength(a);
			VectorSubtract(badthing->s.pos.trBase, gWPArray[tempInt]->origin, a);
			minusLen = VectorLength(a);

			if (plusLen < minusLen)
			{
				if (bs->wpDirection)
				{
					bs->wpDirection = 0;
				}
				else
				{
					bs->wpDirection = 1;
				}

				bs->wpCurrent = gWPArray[tempInt];

				bs->escapeDirTime = level.time + Q_irand(500, 1000);//Q_irand(1000, 1400);

				//trap->Print("Escaping from scary bad thing [%s]\n", badthing->classname);
			}
		}
		//trap->Print("Run away run away run away!\n");
		return;
	}

	distChange = 0; //keep the compiler from complaining

	tempInt = BotGetWeaponRange(bs);

	if (tempInt == BWEAPONRANGE_MELEE)
	{
		distChange = 1;
	}
	else if (tempInt == BWEAPONRANGE_SABER)
	{
		distChange = 1;
	}
	else if (tempInt == BWEAPONRANGE_MID)
	{
		distChange = 128;
	}
	else if (tempInt == BWEAPONRANGE_LONG)
	{
		distChange = 300;
	}

	if (bs->revengeEnemy && bs->revengeEnemy->health > 0 &&
		bs->revengeEnemy->client && bs->revengeEnemy->client->pers.connected == CON_CONNECTED)
	{ //if we hate someone, always try to get to them
		if (bs->wpDestSwitchTime < level.time)
		{
			if (bs->revengeEnemy->client)
			{
				VectorCopy(bs->revengeEnemy->client->ps.origin, usethisvec);
			}
			else
			{
				VectorCopy(bs->revengeEnemy->s.origin, usethisvec);
			}

			tempInt = GetNearestVisibleWP(usethisvec, 0);

			if (tempInt != -1 && TotalTrailDistance(bs->wpCurrent->index, tempInt, bs) != -1)
			{
				bs->wpDestination = gWPArray[tempInt];
				bs->wpDestSwitchTime = level.time + Q_irand(5000, 10000);
			}
		}
	}
	else if (bs->squadLeader && bs->squadLeader->health > 0 &&
		bs->squadLeader->client && bs->squadLeader->client->pers.connected == CON_CONNECTED)
	{
		if (bs->wpDestSwitchTime < level.time)
		{
			if (bs->squadLeader->client)
			{
				VectorCopy(bs->squadLeader->client->ps.origin, usethisvec);
			}
			else
			{
				VectorCopy(bs->squadLeader->s.origin, usethisvec);
			}

			tempInt = GetNearestVisibleWP(usethisvec, 0);

			if (tempInt != -1 && TotalTrailDistance(bs->wpCurrent->index, tempInt, bs) != -1)
			{
				bs->wpDestination = gWPArray[tempInt];
				bs->wpDestSwitchTime = level.time + Q_irand(5000, 10000);
			}
		}
	}
	else if (bs->currentEnemy)
	{
		if (bs->currentEnemy->client)
		{
			VectorCopy(bs->currentEnemy->client->ps.origin, usethisvec);
		}
		else
		{
			VectorCopy(bs->currentEnemy->s.origin, usethisvec);
		}

		bChicken = BotIsAChickenWuss(bs);
		bs->runningToEscapeThreat = bChicken;

		if (bs->frame_Enemy_Len < distChange || (bChicken && bChicken != 2))
		{
			cWPIndex = bs->wpCurrent->index;

			if (bs->frame_Enemy_Len > 400)
			{ //good distance away, start running toward a good place for an item or powerup or whatever
				idleWP = GetBestIdleGoal(bs);

				if (idleWP != -1 && gWPArray[idleWP] && gWPArray[idleWP]->inuse)
				{
					bs->wpDestination = gWPArray[idleWP];
				}
			}
			else if (gWPArray[cWPIndex - 1] && gWPArray[cWPIndex - 1]->inuse &&
				gWPArray[cWPIndex + 1] && gWPArray[cWPIndex + 1]->inuse)
			{
				VectorSubtract(gWPArray[cWPIndex + 1]->origin, usethisvec, a);
				plusLen = VectorLength(a);
				VectorSubtract(gWPArray[cWPIndex - 1]->origin, usethisvec, a);
				minusLen = VectorLength(a);

				if (minusLen > plusLen)
				{
					bs->wpDestination = gWPArray[cWPIndex - 1];
				}
				else
				{
					bs->wpDestination = gWPArray[cWPIndex + 1];
				}
			}
		}
		else if (bChicken != 2 && bs->wpDestSwitchTime < level.time)
		{
			tempInt = GetNearestVisibleWP(usethisvec, 0);

			if (tempInt != -1 && TotalTrailDistance(bs->wpCurrent->index, tempInt, bs) != -1)
			{
				bs->wpDestination = gWPArray[tempInt];

				if (level.gametype == GT_SINGLE_PLAYER)
				{ //be more aggressive
					bs->wpDestSwitchTime = level.time + Q_irand(300, 1000);
				}
				else
				{
					bs->wpDestSwitchTime = level.time + Q_irand(1000, 5000);
				}
			}
		}
	}

	if (!bs->wpDestination && bs->wpDestSwitchTime < level.time)
	{
		//trap->Print("I need something to do\n");
		idleWP = GetBestIdleGoal(bs);

		if (idleWP != -1 && gWPArray[idleWP] && gWPArray[idleWP]->inuse)
		{
			bs->wpDestination = gWPArray[idleWP];
		}
	}
}

//commander CTF AI - tell other bots in the so-called
//"squad" what to do.
void CommanderBotCTFAI(bot_state_t* bs)
{
	int i = 0;
	gentity_t* ent;
	int squadmates = 0;
	gentity_t* squad[MAX_CLIENTS];
	int defendAttackPriority = 0; //0 == attack, 1 == defend
	int guardDefendPriority = 0; //0 == defend, 1 == guard
	int attackRetrievePriority = 0; //0 == retrieve, 1 == attack
	int myFlag = 0;
	int enemyFlag = 0;
	int enemyHasOurFlag = 0;
	int weHaveEnemyFlag = 0;
	int numOnMyTeam = 0;
	//int numOnEnemyTeam = 0;
	int numAttackers = 0;
	//int numDefenders = 0;

	if (level.clients[bs->client].sess.sessionTeam == TEAM_RED)
	{
		myFlag = PW_REDFLAG;
	}
	else
	{
		myFlag = PW_BLUEFLAG;
	}

	if (level.clients[bs->client].sess.sessionTeam == TEAM_RED)
	{
		enemyFlag = PW_BLUEFLAG;
	}
	else
	{
		enemyFlag = PW_REDFLAG;
	}

	while (i < MAX_CLIENTS)
	{
		ent = &g_entities[i];

		if (ent && ent->client)
		{
			if (ent->client->ps.powerups[enemyFlag] && OnSameTeam(&g_entities[bs->client], ent))
			{
				weHaveEnemyFlag = 1;
			}
			else if (ent->client->ps.powerups[myFlag] && !OnSameTeam(&g_entities[bs->client], ent))
			{
				enemyHasOurFlag = 1;
			}

			if (OnSameTeam(&g_entities[bs->client], ent))
			{
				numOnMyTeam++;
			}
			else
			{
				//numOnEnemyTeam++;
			}

			if (botstates[ent->s.number])
			{
				if (botstates[ent->s.number]->ctfState == CTFSTATE_ATTACKER ||
					botstates[ent->s.number]->ctfState == CTFSTATE_RETRIEVAL)
				{
					numAttackers++;
				}
				else
				{
					//numDefenders++;
				}
			}
			else
			{ //assume real players to be attackers in our logic
				numAttackers++;
			}
		}
		i++;
	}

	i = 0;

	while (i < MAX_CLIENTS)
	{
		ent = &g_entities[i];

		if (ent && ent->client && botstates[i] && botstates[i]->squadLeader && botstates[i]->squadLeader->s.number == bs->client && i != bs->client)
		{
			squad[squadmates] = ent;
			squadmates++;
		}

		i++;
	}

	squad[squadmates] = &g_entities[bs->client];
	squadmates++;

	i = 0;

	if (enemyHasOurFlag && !weHaveEnemyFlag)
	{ //start off with an attacker instead of a retriever if we don't have the enemy flag yet so that they can't capture it first.
	  //after that we focus on getting our flag back.
		attackRetrievePriority = 1;
	}

	while (i < squadmates)
	{
		if (squad[i] && squad[i]->client && botstates[squad[i]->s.number])
		{
			if (botstates[squad[i]->s.number]->ctfState != CTFSTATE_GETFLAGHOME)
			{ //never tell a bot to stop trying to bring the flag to the base
				if (defendAttackPriority)
				{
					if (weHaveEnemyFlag)
					{
						if (guardDefendPriority)
						{
							botstates[squad[i]->s.number]->ctfState = CTFSTATE_GUARDCARRIER;
							guardDefendPriority = 0;
						}
						else
						{
							botstates[squad[i]->s.number]->ctfState = CTFSTATE_DEFENDER;
							guardDefendPriority = 1;
						}
					}
					else
					{
						botstates[squad[i]->s.number]->ctfState = CTFSTATE_DEFENDER;
					}
					defendAttackPriority = 0;
				}
				else
				{
					if (enemyHasOurFlag)
					{
						if (attackRetrievePriority)
						{
							botstates[squad[i]->s.number]->ctfState = CTFSTATE_ATTACKER;
							attackRetrievePriority = 0;
						}
						else
						{
							botstates[squad[i]->s.number]->ctfState = CTFSTATE_RETRIEVAL;
							attackRetrievePriority = 1;
						}
					}
					else
					{
						botstates[squad[i]->s.number]->ctfState = CTFSTATE_ATTACKER;
					}
					defendAttackPriority = 1;
				}
			}
			else if ((numOnMyTeam < 2 || !numAttackers) && enemyHasOurFlag)
			{ //I'm the only one on my team who will attack and the enemy has my flag, I have to go after him
				botstates[squad[i]->s.number]->ctfState = CTFSTATE_RETRIEVAL;
			}
		}

		i++;
	}
}

//similar to ctf ai, for siege
void CommanderBotSiegeAI(bot_state_t* bs)
{
	int i = 0;
	int squadmates = 0;
	int commanded = 0;
	int teammates = 0;
	gentity_t* squad[MAX_CLIENTS];
	gentity_t* ent;
	bot_state_t* bst;

	while (i < MAX_CLIENTS)
	{
		ent = &g_entities[i];

		if (ent && ent->client && OnSameTeam(&g_entities[bs->client], ent) && botstates[ent->s.number])
		{
			bst = botstates[ent->s.number];

			if (bst && !bst->isSquadLeader && !bst->state_Forced)
			{
				squad[squadmates] = ent;
				squadmates++;
			}
			else if (bst && !bst->isSquadLeader && bst->state_Forced)
			{ //count them as commanded
				commanded++;
			}
		}

		if (ent && ent->client && OnSameTeam(&g_entities[bs->client], ent))
		{
			teammates++;
		}

		i++;
	}

	if (!squadmates)
	{
		return;
	}

	//tell squad mates to do what I'm doing, up to half of team, let the other half make their own decisions
	i = 0;

	while (i < squadmates && squad[i])
	{
		bst = botstates[squad[i]->s.number];

		if (commanded > teammates / 2)
		{
			break;
		}

		if (bst)
		{
			bst->state_Forced = bs->siegeState;
			bst->siegeState = bs->siegeState;
			commanded++;
		}

		i++;
	}
}

//teamplay ffa squad ai
void BotDoTeamplayAI(bot_state_t* bs)
{
	if (bs->state_Forced)
	{
		bs->teamplayState = bs->state_Forced;
	}

	if (bs->teamplayState == TEAMPLAYSTATE_REGROUP)
	{ //force to find a new leader
		bs->squadLeader = NULL;
		bs->isSquadLeader = 0;
	}
}

//like ctf and siege commander ai, instruct the squad
void CommanderBotTeamplayAI(bot_state_t* bs)
{
	int i = 0;
	int squadmates = 0;
	//int teammates = 0;
	int teammate_indanger = -1;
	int teammate_helped = 0;
	int foundsquadleader = 0;
	int worsthealth = 50;
	gentity_t* squad[MAX_CLIENTS];
	gentity_t* ent;
	bot_state_t* bst;

	while (i < MAX_CLIENTS)
	{
		ent = &g_entities[i];

		if (ent && ent->client && OnSameTeam(&g_entities[bs->client], ent) && botstates[ent->s.number])
		{
			bst = botstates[ent->s.number];

			if (foundsquadleader && bst && bst->isSquadLeader)
			{ //never more than one squad leader
				bst->isSquadLeader = 0;
			}

			if (bst && !bst->isSquadLeader)
			{
				squad[squadmates] = ent;
				squadmates++;
			}
			else if (bst)
			{
				foundsquadleader = 1;
			}
		}

		if (ent && ent->client && OnSameTeam(&g_entities[bs->client], ent))
		{
			//teammates++;

			if (ent->health < worsthealth)
			{
				teammate_indanger = ent->s.number;
				worsthealth = ent->health;
			}
		}

		i++;
	}

	if (!squadmates)
	{
		return;
	}

	i = 0;

	while (i < squadmates && squad[i])
	{
		bst = botstates[squad[i]->s.number];

		if (bst && !bst->state_Forced)
		{ //only order if this guy is not being ordered directly by the real player team leader
			if (teammate_indanger >= 0 && !teammate_helped)
			{ //send someone out to help whoever needs help most at the moment
				bst->teamplayState = TEAMPLAYSTATE_ASSISTING;
				bst->squadLeader = &g_entities[teammate_indanger];
				teammate_helped = 1;
			}
			else if ((teammate_indanger == -1 || teammate_helped) && bst->teamplayState == TEAMPLAYSTATE_ASSISTING)
			{ //no teammates need help badly, but this guy is trying to help them anyway, so stop
				bst->teamplayState = TEAMPLAYSTATE_FOLLOWING;
				bst->squadLeader = &g_entities[bs->client];
			}

			if (bs->squadRegroupInterval < level.time && Q_irand(1, 10) < 5)
			{ //every so often tell the squad to regroup for the sake of variation
				if (bst->teamplayState == TEAMPLAYSTATE_FOLLOWING)
				{
					bst->teamplayState = TEAMPLAYSTATE_REGROUP;
				}

				bs->isSquadLeader = 0;
				bs->squadCannotLead = level.time + 500;
				bs->squadRegroupInterval = level.time + Q_irand(45000, 65000);
			}
		}

		i++;
	}
}

//pick which commander ai to use based on gametype
void CommanderBotAI(bot_state_t* bs)
{
	if (level.gametype == GT_CTF || level.gametype == GT_CTY)
	{
		CommanderBotCTFAI(bs);
	}
	else if (level.gametype == GT_SIEGE)
	{
		CommanderBotSiegeAI(bs);
	}
	else if (level.gametype == GT_TEAM)
	{
		CommanderBotTeamplayAI(bs);
	}
}

//close range combat routines
void MeleeCombatHandling(bot_state_t* bs)
{
	vec3_t usethisvec;
	vec3_t downvec;
	vec3_t midorg;
	vec3_t a;
	vec3_t fwd;
	vec3_t mins, maxs;
	trace_t tr;
	int en_down;
	int me_down;
	int mid_down;

	if (!bs->currentEnemy)
	{
		return;
	}

	if (bs->currentEnemy->client)
	{
		VectorCopy(bs->currentEnemy->client->ps.origin, usethisvec);
	}
	else
	{
		VectorCopy(bs->currentEnemy->s.origin, usethisvec);
	}

	if (bs->meleeStrafeTime < level.time)
	{
		if (bs->meleeStrafeDir)
		{
			bs->meleeStrafeDir = 0;
		}
		else
		{
			bs->meleeStrafeDir = 1;
		}

		bs->meleeStrafeTime = level.time + Q_irand(500, 1800);
	}

	mins[0] = -15;
	mins[1] = -15;
	mins[2] = -24;
	maxs[0] = 15;
	maxs[1] = 15;
	maxs[2] = 32;

	VectorCopy(usethisvec, downvec);
	downvec[2] -= 4096;

	JP_Trace(&tr, usethisvec, mins, maxs, downvec, -1, MASK_SOLID, qfalse, 0, 0);

	en_down = (int)tr.endpos[2];

	VectorCopy(bs->origin, downvec);
	downvec[2] -= 4096;

	JP_Trace(&tr, bs->origin, mins, maxs, downvec, -1, MASK_SOLID, qfalse, 0, 0);

	me_down = (int)tr.endpos[2];

	VectorSubtract(usethisvec, bs->origin, a);
	vectoangles(a, a);
	AngleVectors(a, fwd, NULL, NULL);

	midorg[0] = bs->origin[0] + fwd[0] * bs->frame_Enemy_Len / 2;
	midorg[1] = bs->origin[1] + fwd[1] * bs->frame_Enemy_Len / 2;
	midorg[2] = bs->origin[2] + fwd[2] * bs->frame_Enemy_Len / 2;

	VectorCopy(midorg, downvec);
	downvec[2] -= 4096;

	JP_Trace(&tr, midorg, mins, maxs, downvec, -1, MASK_SOLID, qfalse, 0, 0);

	mid_down = (int)tr.endpos[2];

	if (me_down == en_down &&
		en_down == mid_down)
	{
		VectorCopy(usethisvec, bs->goalPosition);
	}
}



//should we be "leading" our aim with this weapon? And if
//so, by how much?
float BotWeaponCanLead(bot_state_t* bs)
{
	switch (bs->cur_ps.weapon)
	{
	case WP_BRYAR_PISTOL:
	case WP_BRYAR_OLD:
		return 0.5f;
	case WP_BLASTER:
		return 0.35f;
	case WP_BOWCASTER:
		return 0.5f;
	case WP_REPEATER:
		return 0.45f;
	case WP_THERMAL:
		return 0.5f;
	case WP_DEMP2:
		return 0.35f;
	case WP_ROCKET_LAUNCHER:
		return 0.9f;
	case WP_CONCUSSION:
		if (bs->doAltAttack)
			return 0.03f;
		else
			return 0.2f;
	case WP_FLECHETTE:
		return 0.3f;
	case WP_DISRUPTOR:
		if (g_tweakWeapons.integer & WT_PROJ_SNIPER)
			return 0.08f;
		else
			return 0.03f;
	default:
		return 0.0f;
	}
}

float G_NewBotAIGetProjectileSpeed(int weapon, qboolean altFire) {
	float projectileSpeed = 0;

	if (weapon == WP_BRYAR_OLD || weapon == WP_BRYAR_PISTOL || (weapon == WP_REPEATER && !altFire))
		projectileSpeed = 1600;
	else if (weapon == WP_BLASTER && (g_tweakWeapons.integer & WT_TRIBES))
		projectileSpeed = 10440;
	else if (weapon == WP_BLASTER)
		projectileSpeed = 2300;
	else if (weapon == WP_DISRUPTOR && (g_tweakWeapons.integer & WT_PROJ_SNIPER))
		projectileSpeed = 9000;
	else if (weapon == WP_BOWCASTER)
		projectileSpeed = 1300;
	else if (weapon == WP_DEMP2 && !altFire) {
		if (g_tweakWeapons.integer & WT_TRIBES)
			projectileSpeed = 2200;
		else
			projectileSpeed = 1800;
	}
	else if (weapon == WP_REPEATER && altFire) {
		if (g_tweakWeapons.integer & WT_TRIBES)
			projectileSpeed = 1400;
		else
			projectileSpeed = 1100;
	}
	else if (weapon == WP_FLECHETTE && (g_tweakWeapons.integer & WT_STAKE_GUN))
		projectileSpeed = 3000;
	else if ((weapon == WP_FLECHETTE) && altFire)
		projectileSpeed = 1150;
	else if (weapon == WP_FLECHETTE && !altFire)
		projectileSpeed = 3500;
	else if (weapon == WP_REPEATER && (g_tweakWeapons.integer & WT_TRIBES) && altFire)
		projectileSpeed = 2000;
	else if (weapon == WP_ROCKET_LAUNCHER && !altFire) {
		if (g_tweakWeapons.integer & WT_TRIBES)
			projectileSpeed = 2040;
		else
			projectileSpeed = 900;
	}
	else if (weapon == WP_ROCKET_LAUNCHER && altFire)
		projectileSpeed = 450;
	else if (weapon == WP_CONCUSSION && !altFire && (g_tweakWeapons.integer & WT_TRIBES))
		projectileSpeed = 2275;
	else if (weapon == WP_CONCUSSION && !altFire)
		projectileSpeed = 3000;
	else if (weapon == WP_THERMAL)
		projectileSpeed = 900;

	return projectileSpeed * g_projectileVelocityScale.value;
}

/*
trace between us and this new point, if there is something there, see how far away it is, and if its close enough to splash damage us dont fire, or just dont fire at all? - or switch to demp2 alt if it would dmg them
if no LOS, aim at someone else if LOS.. ?
*/
//offset the desired view angles with aim leading in mind
void G_NewBotAIAimLeading(bot_state_t* bs, vec3_t headlevel) {
	vec3_t predictedSpot, a, ang;
	float eta = 0, projectileDrop = 0;
	float projectileSpeed;

	if (!bs->currentEnemy || !bs->currentEnemy->client)
		return;

	projectileSpeed = G_NewBotAIGetProjectileSpeed(bs->cur_ps.weapon, bs->doAltAttack);

	if (projectileSpeed) { //this should be done after playr movement
		if (g_projectileInheritance.value) { //todo still have to teach brodie full inheritence projectiles
			vec3_t botforward;
			AngleVectors(bs->viewangles, botforward, NULL, NULL);
			projectileSpeed += DotProduct(botforward, g_entities[bs->client].client->ps.velocity) * g_projectileInheritance.value;
		}

		eta = (bs->frame_Enemy_Len / projectileSpeed); //TODO: Adjust if its a curved projectile arc
		VectorMA(headlevel, eta, bs->currentEnemy->client->ps.velocity, predictedSpot); //Multiple vel by eta, and add it to their origin to get predicted spot
		if (((bs->cur_ps.weapon == WP_REPEATER) && bs->doAltAttack) ||
			((bs->cur_ps.weapon == WP_DISRUPTOR) && (g_tweakWeapons.integer & WT_PROJ_SNIPER)) ||
			((bs->cur_ps.weapon == WP_ROCKET_LAUNCHER) && !bs->doAltAttack && (g_tweakWeapons.integer & WT_TRIBES)) ||
			((bs->cur_ps.weapon == WP_FLECHETTE) && (g_tweakWeapons.integer & WT_STAKE_GUN)) ||
			((bs->cur_ps.weapon == WP_FLECHETTE) && !(g_tweakWeapons.integer & WT_STAKE_GUN) && bs->doAltAttack) ||
			(bs->cur_ps.weapon == WP_THERMAL) ||
			(g_tweakWeapons.integer & WT_PROJECTILE_GRAVITY)) {
			projectileDrop = (0.5) * (800) * (eta * eta); //If weapon has gravity, compensate to aim higher
			predictedSpot[2] += projectileDrop;
		}
		if (bs->currentEnemy->client->ps.groundEntityNum == ENTITYNUM_NONE) { //In Air
			if (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_LEVITATION) && bs->currentEnemy->client->ps.velocity[2] > JUMP_VELOCITY - 10) { //If person is forcejumping up... assume they will keep forcejumping.. until end of jump?
				const float diff = (predictedSpot[2] - bs->cur_ps.fd.forceJumpZStart);
				if (diff > forceJumpHeight[bs->currentEnemy->client->ps.fd.forcePowerLevel[FP_LEVITATION]]) { //We predict they will be higher than they can possibly jump to, so correct
					predictedSpot[2] -= (diff - forceJumpHeight[bs->currentEnemy->client->ps.fd.forcePowerLevel[FP_LEVITATION]]);
				}
			}
			else if (bs->currentEnemy->client->ps.eFlags & EF_JETPACK_ACTIVE) { //dont predict drops if they are jetting?
			}
			else {
				vec3_t predictedSpotGrav;
				trace_t tr;
				float playerDrop = (0.5f) * g_gravity.value * (eta * eta);

				predictedSpotGrav[0] = predictedSpot[0];
				predictedSpotGrav[1] = predictedSpot[1];
				predictedSpotGrav[2] = predictedSpot[2] - playerDrop;

				JP_Trace(&tr, headlevel, 0, 0, predictedSpotGrav, ENTITYNUM_NONE, MASK_SOLID, qfalse, 0, 0); //eh? do this if any eta tbh.. not just if jumping
				VectorCopy(tr.endpos, predictedSpot);

				//G_CheckGroundPound(bs, eta, tr.fraction * eta); //return if so?

				//now trace from us to pos and see if its a los, if not don't fire? or...,.,.,.,.,
				//def never fire if within selfkill range
				//y headlevel dont work?
			}
		}
	}
	else { //Hitscan, maybe tweak this so it leads a tiny bit for netcode
		vec3_t dir;
		VectorCopy(bs->currentEnemy->client->ps.velocity, dir);
		VectorNormalize(dir);
		VectorMA(headlevel, 4, dir, predictedSpot); //Lead them by 4u in their direction of motion?

		if ((bs->cur_ps.weapon == WP_DEMP2) && bs->doAltAttack && ((bs->currentEnemy->client->ps.groundEntityNum != ENTITYNUM_NONE) || bs->currentEnemy->client->ps.velocity[2] < 0)) { //stupid demp2 delay compensate, only if they are not in air, or in air and moving down
			trace_t tr;
			vec3_t predictedSpotGround;

			VectorMA(headlevel, 0.5f, bs->currentEnemy->client->ps.velocity, predictedSpot); //Lead them by 500ms
			predictedSpotGround[0] = predictedSpot[0];
			predictedSpotGround[1] = predictedSpot[1];
			predictedSpotGround[2] = predictedSpot[2] - 1024;
			JP_Trace(&tr, headlevel, 0, 0, predictedSpotGround, ENTITYNUM_NONE, MASK_SOLID, qfalse, 0, 0); //aim at ground below them
			VectorCopy(tr.endpos, predictedSpot);

			//G_CheckGroundPound(bs, 0.2f, tr.fraction * 0.2f); //return if so?
		}
	}

	if (bs->cur_ps.weapon == WP_SABER && bs->cur_ps.saberMove >= 4) { //Poke
		vec3_t saberDiff;
		VectorSubtract(bs->currentEnemy->client->ps.origin, g_entities[bs->client].client->saber[0].blade[0].trail.tip, saberDiff);
		VectorAdd(saberDiff, predictedSpot, predictedSpot);
	}


	VectorSubtract(predictedSpot, bs->eye, a);
	vectoangles(a, ang);
	VectorCopy(ang, bs->goalAngles);

	if (bs->cur_ps.weapon == WP_SABER && bs->cur_ps.saberMove >= 4) { //Poke and Wiggle
		if (level.time % 100 > 50) {
			bs->goalAngles[YAW] += 3.0f;
		}
		else {
			bs->goalAngles[YAW] -= 3.0f;
		}

		if (level.time % 200 > 100) {
			bs->goalAngles[PITCH] += 6.0f;
		}
		else {
			bs->goalAngles[PITCH] -= 6.0f;
		}

		if (bs->cur_ps.saberMove == LS_A_T2B) {
			if (bs->cur_ps.torsoTimer > 400) { //aim lower at start of red swing
				bs->goalAngles[PITCH] += 45;
			}
			else if (bs->cur_ps.torsoTimer < 300) {
				bs->goalAngles[PITCH] -= 45.0f;
			}
		}
		else if (bs->cur_ps.saberMove == LS_S_R2L) {//Start of yellow horizontal
			bs->goalAngles[YAW] += 45;
		}
		else if (bs->cur_ps.saberMove == LS_A_R2L) {//yellow horizontal
			if (level.time % 200 > 100)
				bs->goalAngles[YAW] += 20;
			else
				bs->goalAngles[YAW] += 20;

		}
		else if (bs->cur_ps.saberMove == LS_R_R2L) {//end of yellow horizontal
			//bs->goalAngles[YAW] -= 30;
		}

		bs->goalAngles[YAW] = AngleNormalize360(bs->goalAngles[YAW]);
		bs->goalAngles[PITCH] = AngleNormalize360(bs->goalAngles[PITCH]);
	}
}


//offset the desired view angles with aim leading in mind
void BotAimLeading(bot_state_t* bs, vec3_t headlevel, float leadAmount)
{
	int x;
	vec3_t predictedSpot;
	vec3_t movementVector;
	vec3_t a, ang;
	float vtotal;

	if (!bs->currentEnemy ||
		!bs->currentEnemy->client)
	{
		return;
	}

	if (!bs->frame_Enemy_Len)
	{
		return;
	}

	vtotal = 0;

	if (bs->currentEnemy->client->ps.velocity[0] < 0)
	{
		vtotal += -bs->currentEnemy->client->ps.velocity[0];
	}
	else
	{
		vtotal += bs->currentEnemy->client->ps.velocity[0];
	}

	if (bs->currentEnemy->client->ps.velocity[1] < 0)
	{
		vtotal += -bs->currentEnemy->client->ps.velocity[1];
	}
	else
	{
		vtotal += bs->currentEnemy->client->ps.velocity[1];
	}

	if (bs->currentEnemy->client->ps.velocity[2] < 0)
	{
		vtotal += -bs->currentEnemy->client->ps.velocity[2];
	}
	else
	{
		vtotal += bs->currentEnemy->client->ps.velocity[2];
	}

	//G_Printf("Leadin target with a velocity total of %f\n", vtotal);

	VectorCopy(bs->currentEnemy->client->ps.velocity, movementVector);

	VectorNormalize(movementVector);

	x = bs->frame_Enemy_Len * leadAmount; //hardly calculated with an exact science, but it works

	if (vtotal > 400)
	{
		vtotal = 400;
	}

	if (vtotal)
	{
		x = (bs->frame_Enemy_Len * 0.9) * leadAmount * (vtotal * 0.0012); //hardly calculated with an exact science, but it works
	}
	else
	{
		x = (bs->frame_Enemy_Len * 0.9) * leadAmount; //hardly calculated with an exact science, but it works
	}

	predictedSpot[0] = headlevel[0] + (movementVector[0] * x);
	predictedSpot[1] = headlevel[1] + (movementVector[1] * x);
	predictedSpot[2] = headlevel[2] + (movementVector[2] * x);

	VectorSubtract(predictedSpot, bs->eye, a);
	vectoangles(a, ang);
	VectorCopy(ang, bs->goalAngles);
}

//wobble our aim around based on our sk1llz
void BotAimOffsetGoalAngles(bot_state_t* bs)
{
	int i;
	float accVal;
	i = 0;

	if (bs->skills.perfectaim)
	{
		return;
	}

	if (bs->aimOffsetTime > level.time)
	{
		if (bs->aimOffsetAmtYaw)
		{
			bs->goalAngles[YAW] += bs->aimOffsetAmtYaw;
		}

		if (bs->aimOffsetAmtPitch)
		{
			bs->goalAngles[PITCH] += bs->aimOffsetAmtPitch;
		}

		while (i <= 2)
		{
			if (bs->goalAngles[i] > 360)
			{
				bs->goalAngles[i] -= 360;
			}

			if (bs->goalAngles[i] < 0)
			{
				bs->goalAngles[i] += 360;
			}

			i++;
		}
		return;
	}

	accVal = bs->skills.accuracy / bs->settings.skill;

	if (bs->currentEnemy && BotMindTricked(bs->client, bs->currentEnemy->s.number))
	{ //having to judge where they are by hearing them, so we should be quite inaccurate here
		accVal *= 7;

		if (accVal < 30)
		{
			accVal = 30;
		}
	}

	if (bs->revengeEnemy && bs->revengeHateLevel &&
		bs->currentEnemy == bs->revengeEnemy)
	{ //bot becomes more skilled as anger level raises
		accVal = accVal / bs->revengeHateLevel;
	}

	if (bs->currentEnemy && bs->frame_Enemy_Vis)
	{ //assume our goal is aiming at the enemy, seeing as he's visible and all
		if (!bs->currentEnemy->s.pos.trDelta[0] &&
			!bs->currentEnemy->s.pos.trDelta[1] &&
			!bs->currentEnemy->s.pos.trDelta[2])
		{
			accVal = 0; //he's not even moving, so he shouldn't really be hard to hit.
		}
		else
		{
			accVal += accVal * 0.25; //if he's moving he's this much harder to hit
		}

		if (g_entities[bs->client].s.pos.trDelta[0] ||
			g_entities[bs->client].s.pos.trDelta[1] ||
			g_entities[bs->client].s.pos.trDelta[2])
		{
			accVal += accVal * 0.15; //make it somewhat harder to aim if we're moving also
		}
	}

	if (accVal > 90)
	{
		accVal = 90;
	}
	if (accVal < 1)
	{
		accVal = 0;
	}

	if (!accVal)
	{
		bs->aimOffsetAmtYaw = 0;
		bs->aimOffsetAmtPitch = 0;
		return;
	}

	if (rand() % 10 <= 5)
	{
		bs->aimOffsetAmtYaw = rand() % (int)accVal;
	}
	else
	{
		bs->aimOffsetAmtYaw = -(rand() % (int)accVal);
	}

	if (rand() % 10 <= 5)
	{
		bs->aimOffsetAmtPitch = rand() % (int)accVal;
	}
	else
	{
		bs->aimOffsetAmtPitch = -(rand() % (int)accVal);
	}

	bs->aimOffsetTime = level.time + rand() % 500 + 200;
}

//do we want to alt fire with this weapon?
int ShouldSecondaryFire(bot_state_t* bs)
{
	int weap;
	int dif;
	float rTime;

	weap = bs->cur_ps.weapon;

	if (bs->cur_ps.ammo[weaponData[weap].ammoIndex] < weaponData[weap].altEnergyPerShot)
	{
		return 0;
	}

	if (bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT && bs->cur_ps.weapon == WP_ROCKET_LAUNCHER)
	{
		float heldTime = (level.time - bs->cur_ps.weaponChargeTime);

		rTime = bs->cur_ps.rocketLockTime;

		if (rTime < 1)
		{
			rTime = bs->cur_ps.rocketLastValidTime;
		}

		if (heldTime > 5000)
		{ //just give up and release it if we can't manage a lock in 5 seconds
			return 2;
		}

		if (rTime > 0)
		{
			dif = (level.time - rTime) / (1200.0f / 16.0f);

			if (dif >= 10)
			{
				return 2;
			}
			else if (bs->frame_Enemy_Len > 250)
			{
				return 1;
			}
		}
		else if (bs->frame_Enemy_Len > 250)
		{
			return 1;
		}
	}
	else if ((bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT) && (level.time - bs->cur_ps.weaponChargeTime) > bs->altChargeTime)
	{
		return 2;
	}
	else if (bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT)
	{
		return 1;
	}

	if (weap == WP_BRYAR_PISTOL && bs->frame_Enemy_Len < 300)
	{
		return 1;
	}
	else if (weap == WP_BOWCASTER && bs->frame_Enemy_Len > 300)
	{
		return 1;
	}
	else if (weap == WP_REPEATER && bs->frame_Enemy_Len < 500 && bs->frame_Enemy_Len > 250)
	{
		return 1;
	}
	else if (weap == WP_BLASTER && bs->frame_Enemy_Len < 1000)
	{
		return 1;
	}
	else if (weap == WP_FLECHETTE && bs->frame_Enemy_Len < 500 && bs->frame_Enemy_Len > 250)
	{
		return 1;
	}

	return 0;
}

//standard weapon combat routines // Niksata Edit
int CombatBotAI(bot_state_t* bs, float thinktime)
{
	vec3_t eorg, a;
	int secFire;
	float fovcheck;

	if (!bs->currentEnemy)
	{
		return 0;
	}

	if (bs->currentEnemy->client)
	{
		VectorCopy(bs->currentEnemy->client->ps.origin, eorg);
	}
	else
	{
		VectorCopy(bs->currentEnemy->s.origin, eorg);
	}

	VectorSubtract(eorg, bs->eye, a);
	vectoangles(a, a);

	if (BotGetWeaponRange(bs) == BWEAPONRANGE_SABER)
	{
		if (bs->frame_Enemy_Len <= SABER_ATTACK_RANGE)
		{
			bs->doAttack = 1;
		}
	}
	else if (BotGetWeaponRange(bs) == BWEAPONRANGE_MELEE)
	{
		if (bs->frame_Enemy_Len <= MELEE_ATTACK_RANGE)
		{
			bs->doAttack = 1;
		}
	}
	else
	{
		if (bs->cur_ps.weapon == WP_THERMAL || bs->cur_ps.weapon == WP_ROCKET_LAUNCHER)
		{ //be careful with the hurty weapons
			fovcheck = 40;

			if (bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT &&
				bs->cur_ps.weapon == WP_ROCKET_LAUNCHER)
			{ //if we're charging the weapon up then we can hold fire down within a normal fov
				fovcheck = 60;
			}
		}
		else
		{
			fovcheck = 60;
		}

		if (bs->cur_ps.weaponstate == WEAPON_CHARGING ||
			bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT)
		{
			fovcheck = 160;
		}

		if (bs->frame_Enemy_Len < 128)
		{
			fovcheck *= 2;
		}

		if (InFieldOfVision(bs->viewangles, fovcheck, a))
		{
			if (bs->cur_ps.weapon == WP_THERMAL)
			{
				if (((level.time - bs->cur_ps.weaponChargeTime) < (bs->frame_Enemy_Len * 2) &&
					(level.time - bs->cur_ps.weaponChargeTime) < 4000 &&
					bs->frame_Enemy_Len > 64) ||
					(bs->cur_ps.weaponstate != WEAPON_CHARGING &&
						bs->cur_ps.weaponstate != WEAPON_CHARGING_ALT))
				{
					if (bs->cur_ps.weaponstate != WEAPON_CHARGING && bs->cur_ps.weaponstate != WEAPON_CHARGING_ALT)
					{
						if (bs->frame_Enemy_Len > 512 && bs->frame_Enemy_Len < 800)
						{
							bs->doAltAttack = 1;
							//bs->doAttack = 1;
						}
						else
						{
							bs->doAttack = 1;
							//bs->doAltAttack = 1;
						}
					}

					if (bs->cur_ps.weaponstate == WEAPON_CHARGING)
					{
						bs->doAttack = 1;
					}
					else if (bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT)
					{
						bs->doAltAttack = 1;
					}
				}
			}
			else
			{
				secFire = ShouldSecondaryFire(bs);

				if (bs->cur_ps.weaponstate != WEAPON_CHARGING_ALT &&
					bs->cur_ps.weaponstate != WEAPON_CHARGING)
				{
					bs->altChargeTime = Q_irand(500, 1000);
				}

				if (secFire == 1)
				{
					bs->doAltAttack = 1;
				}
				else if (!secFire)
				{
					if (bs->cur_ps.weapon != WP_THERMAL)
					{
						if (bs->cur_ps.weaponstate != WEAPON_CHARGING ||
							bs->altChargeTime > (level.time - bs->cur_ps.weaponChargeTime))
						{
							bs->doAttack = 1;
						}
					}
					else
					{
						bs->doAttack = 1;
					}
				}

				if (secFire == 2)
				{ //released a charge
					return 1;
				}
			}
		}
	}

	return 0;
} // Niksata Edit

// ========================================
// BOT FALLBACK NAVIGATION - SMART PATHFINDING
// ========================================
// Helper function declarations
qboolean IsPositionReachable(bot_state_t* bs, vec3_t targetPos);
qboolean FollowGroundContours(bot_state_t* bs, vec3_t forward);
qboolean ExpandingSpiralSearch(bot_state_t* bs);
qboolean MoveTowardLastWaypoint(bot_state_t* bs);

//we messed up and got off the normal path, let's fall // Niksata Edit
//back to jumping around and turning in random
//directions off walls to see if we can get back to a
//good place.
int BotFallbackNavigation(bot_state_t* bs) {
	vec3_t mins, maxs, forward, right, up, end;
	trace_t tr;
	float wallDistance;

	// PERSISTENT EXPLORATION STATE
	static vec3_t explorationTarget[MAX_CLIENTS];
	static int explorationTimer[MAX_CLIENTS];
	static int stuckCounter[MAX_CLIENTS];
	static vec3_t lastPosition[MAX_CLIENTS];
	static int lastWallHitTime[MAX_CLIENTS];
	static vec3_t lastMovementDirection[MAX_CLIENTS];

	// COMBAT PRIORITY
	if (bs->currentEnemy && bs->frame_Enemy_Vis) {
		return 0;
	}

	// INITIALIZE EXPLORATION STATE
	if (explorationTimer[bs->client] == 0) {
		VectorClear(explorationTarget[bs->client]);
		explorationTimer[bs->client] = 1;
		stuckCounter[bs->client] = 0;
		VectorCopy(bs->origin, lastPosition[bs->client]);
		lastWallHitTime[bs->client] = 0;
		VectorClear(lastMovementDirection[bs->client]);
	}

	// ================================
	// REAL-TIME MOVEMENT VALIDATION
	// ================================
	float movedDistance = Distance(bs->origin, lastPosition[bs->client]);

	// Check if we're actually moving toward our target
	if (explorationTarget[bs->client][0] != 0 || explorationTarget[bs->client][1] != 0) {
		vec3_t targetDirection;
		VectorSubtract(explorationTarget[bs->client], bs->origin, targetDirection);
		targetDirection[2] = 0;
		VectorNormalize(targetDirection);

		float movementAlignment = DotProduct(targetDirection, lastMovementDirection[bs->client]);

		if (movedDistance < 10.0f || movementAlignment < 0.3f) {
			stuckCounter[bs->client]++;

			if (stuckCounter[bs->client] > 5) {
				VectorClear(explorationTarget[bs->client]);
				stuckCounter[bs->client] = 0;
				lastWallHitTime[bs->client] = level.time + 1000;
			}
		}
		else {
			stuckCounter[bs->client] = 0;
		}
	}

	VectorCopy(bs->origin, lastPosition[bs->client]);

	// ================================
	// CONTINUOUS WALL & LEDGE DETECTION
	// ================================
	VectorSet(mins, -15, -15, -8);
	VectorSet(maxs, 15, 15, 32);
	AngleVectors(bs->viewangles, forward, right, up);

	// Check wall in front
	VectorMA(bs->origin, 32, forward, end);
	JP_Trace(&tr, bs->origin, mins, maxs, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);
	wallDistance = tr.fraction * 32;

	// Check for ledge in front
	vec3_t ledgeCheck, ledgeTest;
	VectorMA(bs->origin, 64, forward, ledgeCheck);
	ledgeCheck[2] += 50;
	VectorCopy(ledgeCheck, ledgeTest);
	ledgeTest[2] -= 300;

	JP_Trace(&tr, ledgeCheck, NULL, NULL, ledgeTest, bs->client, MASK_SOLID, qfalse, 0, 0);

	// LEDGE DETECTION - if no ground within reasonable distance
	qboolean isLedgeAhead = (tr.fraction >= 1.0f) || (ledgeCheck[2] - tr.endpos[2] > 120);

	// IMMEDIATE WALL/LEDGE RESPONSE
	if (wallDistance < 20 || isLedgeAhead) {
		if (lastWallHitTime[bs->client] < level.time) {
			// Smart turn away from danger
			vec3_t leftDir, rightDir;
			VectorMA(forward, 32, right, end);
			JP_Trace(&tr, bs->origin, mins, maxs, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);
			float rightDistance = tr.fraction * 32;

			VectorMA(forward, -32, right, end);
			JP_Trace(&tr, bs->origin, mins, maxs, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);
			float leftDistance = tr.fraction * 32;

			// Test for ledges on both sides
			qboolean leftLedge = qfalse, rightLedge = qfalse;

			// Check left side for ledge
			VectorMA(bs->origin, -32, right, ledgeCheck);
			ledgeCheck[2] += 50;
			VectorCopy(ledgeCheck, ledgeTest);
			ledgeTest[2] -= 300;
			JP_Trace(&tr, ledgeCheck, NULL, NULL, ledgeTest, bs->client, MASK_SOLID, qfalse, 0, 0);
			leftLedge = (tr.fraction >= 1.0f) || (ledgeCheck[2] - tr.endpos[2] > 120);

			// Check right side for ledge
			VectorMA(bs->origin, 32, right, ledgeCheck);
			ledgeCheck[2] += 50;
			VectorCopy(ledgeCheck, ledgeTest);
			ledgeTest[2] -= 300;
			JP_Trace(&tr, ledgeCheck, NULL, NULL, ledgeTest, bs->client, MASK_SOLID, qfalse, 0, 0);
			rightLedge = (tr.fraction >= 1.0f) || (ledgeCheck[2] - tr.endpos[2] > 120);

			// Choose safest direction
			if (!leftLedge && leftDistance > rightDistance) {
				bs->ideal_viewangles[YAW] -= 60; // Turn left
			}
			else if (!rightLedge && rightDistance >= leftDistance) {
				bs->ideal_viewangles[YAW] += 60; // Turn right
			}
			else {
				// Both directions dangerous, turn around
				bs->ideal_viewangles[YAW] += 180;
			}

			VectorClear(explorationTarget[bs->client]);
			lastWallHitTime[bs->client] = level.time + 500;
		}

		// Move backward to get away from danger
		VectorScale(forward, -1, forward);
		trap->EA_Move(bs->client, forward, 2000);
		return 1;
	}

	// ================================
	// SAFE EXPLORATION TARGET GENERATION
	// ================================
	vec3_t moveDir;
	qboolean hasTarget = qfalse;

	if (explorationTarget[bs->client][0] != 0 ||
		explorationTarget[bs->client][1] != 0 ||
		explorationTarget[bs->client][2] != 0) {

		VectorSubtract(explorationTarget[bs->client], bs->origin, moveDir);
		float targetDistance = VectorLength(moveDir);

		if (targetDistance > 50.0f) {
			VectorNormalize(moveDir);
			hasTarget = qtrue;
		}
		else {
			VectorClear(explorationTarget[bs->client]);
		}
	}

	// Generate new SAFE exploration targets
	if (!hasTarget && lastWallHitTime[bs->client] < level.time) {
		// Try multiple directions with safety checks
		for (int attempts = 0; attempts < 12; attempts++) { // More attempts
			float randomAngle = Q_irand(0, 359);
			float randomDistance = Q_irand(150, 400); // Shorter, safer distances

			vec3_t testPos;
			testPos[0] = bs->origin[0] + cos(DEG2RAD(randomAngle)) * randomDistance;
			testPos[1] = bs->origin[1] + sin(DEG2RAD(randomAngle)) * randomDistance;
			testPos[2] = bs->origin[2];

			// Enhanced safety check
			if (IsPositionReachable(bs, testPos)) {
				VectorCopy(testPos, explorationTarget[bs->client]);
				VectorSubtract(testPos, bs->origin, moveDir);
				VectorNormalize(moveDir);
				hasTarget = qtrue;
				break;
			}
		}
	}

	// Execute movement
	if (hasTarget) {
		VectorCopy(moveDir, lastMovementDirection[bs->client]);

		vectoangles(moveDir, bs->goalAngles);
		bs->goalAngles[PITCH] = 0;
		bs->goalAngles[ROLL] = 0;

		trap->EA_Move(bs->client, moveDir, 3500); // Slightly slower for safety

		VectorMA(bs->origin, 250.0f, moveDir, bs->goalPosition);

		return 1;
	}

	// Safe emergency movement
	if (stuckCounter[bs->client] > 20 && lastWallHitTime[bs->client] < level.time) {
		// Try to move toward safer, lower ground
		float randomAngle = Q_irand(0, 359);
		forward[0] = cos(DEG2RAD(randomAngle));
		forward[1] = sin(DEG2RAD(randomAngle));
		forward[2] = 0;

		// Check if this direction is safe
		vec3_t testPos;
		VectorMA(bs->origin, 200, forward, testPos);

		if (IsPositionReachable(bs, testPos)) {
			trap->EA_Move(bs->client, forward, 2500);
			bs->ideal_viewangles[YAW] = randomAngle;
			VectorCopy(forward, lastMovementDirection[bs->client]);
			lastWallHitTime[bs->client] = level.time + 2000;
			return 1;
		}
	}

	return 0;
}

// ========================================
// SMART EXPLORATION SYSTEM
// ========================================
int SmartExploration(bot_state_t* bs) {
	vec3_t forward, right, up;
	AngleVectors(bs->viewangles, forward, right, up);

	// Strategy 1: Follow ground contours
	if (FollowGroundContours(bs, forward)) {
		return 1;
	}

	// Strategy 2: Expanding spiral search
	if (ExpandingSpiralSearch(bs)) {
		return 1;
	}

	// Strategy 3: Directional bias toward last known waypoint
	if (MoveTowardLastWaypoint(bs)) {
		return 1;
	}

	return 0;
}

// ========================================
// GROUND CONTOUR FOLLOWING
// ========================================
qboolean FollowGroundContours(bot_state_t* bs, vec3_t forward) {
	vec3_t mins, maxs, up;
	trace_t tr;

	VectorSet(mins, -15, -15, 0);
	VectorSet(maxs, 15, 15, 32);
	AngleVectors(bs->viewangles, NULL, NULL, up);

	vec3_t groundPoints[7]; // Center + 6 directions
	float angles[7] = { 0, 30, -30, 60, -60, 90, -90 };
	qboolean validPath[7] = { qfalse };

	// Sample ground in multiple directions
	for (int i = 0; i < 7; i++) {
		vec3_t testDir, testPoint, groundTest;

		// Rotate forward direction by angle
		RotatePointAroundVector(testDir, forward, up, angles[i]);

		// Test ground at different distances
		for (float dist = 100; dist <= 500; dist += 100) {
			VectorMA(bs->origin, dist, testDir, testPoint);
			groundTest[0] = testPoint[0];
			groundTest[1] = testPoint[1];
			groundTest[2] = testPoint[2] - 600; // Trace further down

			JP_Trace(&tr, testPoint, NULL, NULL, groundTest, bs->client, MASK_SOLID, qfalse, 0, 0);

			if (tr.fraction < 1.0f && fabs(tr.endpos[2] - bs->origin[2]) < 200) {
				VectorCopy(tr.endpos, groundPoints[i]);
				validPath[i] = qtrue;
				break;
			}
		}
	}

	// Find best path (longest valid)
	int bestPath = -1;
	float bestDistance = 0;

	for (int i = 1; i < 7; i++) { // Skip center (0)
		if (validPath[i]) {
			float dist = Distance(bs->origin, groundPoints[i]);
			if (dist > bestDistance) {
				bestDistance = dist;
				bestPath = i;
			}
		}
	}

	if (bestPath != -1) {
		vec3_t moveDir;
		VectorSubtract(groundPoints[bestPath], bs->origin, moveDir);
		VectorNormalize(moveDir);

		VectorMA(bs->origin, bestDistance * 0.8f, moveDir, bs->goalPosition);
		vectoangles(moveDir, bs->goalAngles);
		bs->goalAngles[PITCH] = 0;
		bs->goalAngles[ROLL] = 0;
		return qtrue;
	}

	return qfalse;
}

// ========================================
// EXPANDING SPIRAL SEARCH
// ========================================
qboolean ExpandingSpiralSearch(bot_state_t* bs) {
	float spiralRadius = 100.0f;
	float maxRadius = 800.0f;

	while (spiralRadius <= maxRadius) {
		for (float angle = 0; angle < 360; angle += 30) { // More granular
			vec3_t testPos;
			testPos[0] = bs->origin[0] + cos(DEG2RAD(angle)) * spiralRadius;
			testPos[1] = bs->origin[1] + sin(DEG2RAD(angle)) * spiralRadius;
			testPos[2] = bs->origin[2];

			// Check if position is reachable
			if (IsPositionReachable(bs, testPos)) {
				VectorCopy(testPos, bs->goalPosition);
				vectoangles(testPos, bs->goalAngles);
				bs->goalAngles[PITCH] = 0;
				bs->goalAngles[ROLL] = 0;
				return qtrue;
			}
		}
		spiralRadius += 100.0f; // Expand search
	}

	return qfalse;
}

// ========================================
// MOVE TOWARD LAST WAYPOINT
// ========================================
qboolean MoveTowardLastWaypoint(bot_state_t* bs) {
	if (bs->lastKnownWaypointPos[0] == 0 &&
		bs->lastKnownWaypointPos[1] == 0 &&
		bs->lastKnownWaypointPos[2] == 0) {
		return qfalse; // No known waypoint
	}

	vec3_t moveDir;
	VectorSubtract(bs->lastKnownWaypointPos, bs->origin, moveDir);
	moveDir[2] = 0; // Keep on ground plane

	float distance = VectorLength(moveDir);
	if (distance < 50.0f) {
		return qfalse; // Already at waypoint
	}

	VectorNormalize(moveDir);

	// Check if path is clear
	vec3_t testPos;
	VectorMA(bs->origin, min(distance, 300.0f), moveDir, testPos);

	vec3_t mins, maxs;
	VectorSet(mins, -15, -15, 0);
	VectorSet(maxs, 15, 15, 32);

	trace_t tr;
	JP_Trace(&tr, bs->origin, mins, maxs, testPos, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

	if (tr.fraction >= 0.7f) {
		// Path is clear, move toward waypoint
		VectorMA(bs->origin, distance * 0.9f, moveDir, bs->goalPosition);
		vectoangles(moveDir, bs->goalAngles);
		bs->goalAngles[PITCH] = 0;
		bs->goalAngles[ROLL] = 0;
		return qtrue;
	}

	return qfalse;
}

// ========================================
// POSITION REACHABILITY TESTING
// ========================================
qboolean IsPositionReachable(bot_state_t* bs, vec3_t targetPos) {
	vec3_t mins, maxs;
	trace_t tr;

	VectorSet(mins, -15, -15, 0);
	VectorSet(maxs, 15, 15, 32);

	// Check direct path
	JP_Trace(&tr, bs->origin, mins, maxs, targetPos, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);
	if (tr.fraction < 0.7f) return qfalse;

	// ========================================
	// ENHANCED LEDGE DETECTION SYSTEM
	// ========================================

	// Check ground at multiple points along the path
	vec3_t pathDirection, checkPoint, groundCheck, groundTest;
	VectorSubtract(targetPos, bs->origin, pathDirection);
	pathDirection[2] = 0; // Keep on ground plane
	float pathDistance = VectorNormalize(pathDirection);

	// Check every 100 units along the path
	int checks = (int)(pathDistance / 100.0f) + 1;
	for (int i = 1; i <= checks; i++) {
		float checkDistance = min(i * 100.0f, pathDistance);
		VectorMA(bs->origin, checkDistance, pathDirection, checkPoint);

		// Check ground at this point
		VectorCopy(checkPoint, groundCheck);
		groundCheck[2] += 50; // Start 50 units above
		VectorCopy(groundCheck, groundTest);
		groundTest[2] -= 400; // Trace 400 units down

		JP_Trace(&tr, groundCheck, NULL, NULL, groundTest, bs->client, MASK_SOLID, qfalse, 0, 0);

		// No ground found - dangerous drop
		if (tr.fraction >= 1.0f) {
			return qfalse;
		}

		// Check if ground is too far below (ledge)
		float groundHeight = tr.endpos[2];
		float currentHeight = checkPoint[2];
		float heightDiff = currentHeight - groundHeight;

		// If drop is more than 150 units, it's dangerous
		if (heightDiff > 150) {
			return qfalse;
		}

		// Check if ground is too far above (wall/cliff)
		if (groundHeight - currentHeight > 100) {
			return qfalse;
		}
	}

	// Final ground check at target
	VectorCopy(targetPos, groundCheck);
	groundCheck[2] += 50;
	VectorCopy(groundCheck, groundTest);
	groundTest[2] -= 400;

	JP_Trace(&tr, groundCheck, NULL, NULL, groundTest, bs->client, MASK_SOLID, qfalse, 0, 0);

	if (tr.fraction >= 1.0f) return qfalse; // No ground at target

	// Final height check
	float finalHeightDiff = fabs(tr.endpos[2] - bs->origin[2]);
	if (finalHeightDiff > 200) return qfalse;

	return qtrue;
} // Niksata Edit

int BotTryAnotherWeapon(bot_state_t* bs)
{ //out of ammo, resort to the first weapon we come across that has ammo
	int i;

	i = 1;

	while (i < WP_NUM_WEAPONS)
	{
		if (bs->cur_ps.ammo[weaponData[i].ammoIndex] >= weaponData[i].energyPerShot &&
			(bs->cur_ps.stats[STAT_WEAPONS] & (1 << i)))
		{
			bs->virtualWeapon = i;
			BotSelectWeapon(bs->client, i);
			//bs->cur_ps.weapon = i;
			//level.clients[bs->client].ps.weapon = i;
			return 1;
		}

		i++;
	}

	if (bs->cur_ps.weapon != 1 && bs->virtualWeapon != 1)
	{ //should always have this.. shouldn't we?
		bs->virtualWeapon = 1;
		BotSelectWeapon(bs->client, 1);
		//bs->cur_ps.weapon = 1;
		//level.clients[bs->client].ps.weapon = 1;
		return 1;
	}

	return 0;
}

//is this weapon available to us?
qboolean BotWeaponSelectable(bot_state_t* bs, int weapon)
{
	if (weapon == WP_NONE)
	{
		return qfalse;
	}

	if (bs->cur_ps.ammo[weaponData[weapon].ammoIndex] >= weaponData[weapon].energyPerShot &&
		(bs->cur_ps.stats[STAT_WEAPONS] & (1 << weapon)))
	{
		return qtrue;
	}

	return qfalse;
}

qboolean BotWeaponSelectableAltFire(bot_state_t* bs, int weapon)
{
	if (weapon == WP_NONE)
	{
		return qfalse;
	}

	if (bs->cur_ps.ammo[weaponData[weapon].ammoIndex] >= weaponData[weapon].altEnergyPerShot &&
		(bs->cur_ps.stats[STAT_WEAPONS] & (1 << weapon)))
	{
		return qtrue;
	}

	return qfalse;
}

//select the best weapon we can
int BotSelectIdealWeapon(bot_state_t* bs)
{
	int i;
	int bestweight = -1;
	int bestweapon = 0;

	i = 0;

	if (g_newBotAI.integer && (bs->cur_ps.stats[STAT_WEAPONS] & (1 << WP_SABER))) { //always use saber for new bot.. sad hack
		BotSelectWeapon(bs->client, WP_SABER);
		return 0;
	}

	while (i < WP_NUM_WEAPONS)
	{
		if (bs->cur_ps.ammo[weaponData[i].ammoIndex] >= weaponData[i].energyPerShot &&
			bs->botWeaponWeights[i] > bestweight &&
			(bs->cur_ps.stats[STAT_WEAPONS] & (1 << i)))
		{
			if (i == WP_THERMAL)
			{ //special case..
				if (bs->currentEnemy && bs->frame_Enemy_Len < 700)
				{
					bestweight = bs->botWeaponWeights[i];
					bestweapon = i;
				}
			}
			else
			{
				bestweight = bs->botWeaponWeights[i];
				bestweapon = i;
			}
		}

		i++;
	}

	if (bs->currentEnemy && bs->frame_Enemy_Len < 300 &&
		bestweapon == WP_BRYAR_PISTOL &&
		(bs->cur_ps.stats[STAT_WEAPONS] & (1 << WP_SABER)))
	{
		bestweapon = WP_SABER;
		bestweight = 1;
	}

	if (bs->currentEnemy)
	{
		if (bs->frame_Enemy_Len > 1000)
		{
			if (BotWeaponSelectable(bs, WP_DISRUPTOR))
			{
				bestweapon = WP_DISRUPTOR;
				bestweight = 1;
			}
			else if (BotWeaponSelectable(bs, WP_DEMP2))
			{
				bestweapon = WP_DEMP2;
				bestweight = 1;
			}
			else if (BotWeaponSelectable(bs, WP_BLASTER))
			{
				bestweapon = WP_BLASTER;
				bestweight = 1;
			}
		}
		else if (bs->frame_Enemy_Len > 300 && bs->frame_Enemy_Len < 900)
		{
			if (BotWeaponSelectableAltFire(bs, WP_BLASTER))
			{
				bestweapon = WP_BLASTER;
				bestweight = 1;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_REPEATER))
			{
				bestweapon = WP_REPEATER;
				bestweight = 1;
			}
			else if (BotWeaponSelectable(bs, WP_DISRUPTOR))
			{
				bestweapon = WP_DISRUPTOR;
				bestweight = 1;
			}
		}
		else if (bs->frame_Enemy_Len < 150)
		{
			if (BotWeaponSelectable(bs, WP_FLECHETTE))
			{
				bestweapon = WP_FLECHETTE;
				bestweight = 1;
			}
			else if (BotWeaponSelectable(bs, WP_REPEATER))
			{
				bestweapon = WP_REPEATER;
				bestweight = 1;
			}
			else if (BotWeaponSelectable(bs, WP_ROCKET_LAUNCHER))
			{
				bestweapon = WP_ROCKET_LAUNCHER;
				bestweight = 1;
			}
			else if (BotWeaponSelectable(bs, WP_CONCUSSION))
			{
				bestweapon = WP_CONCUSSION;
				bestweight = 1;
			}
			else if (BotWeaponSelectable(bs, WP_BLASTER))
			{
				bestweapon = WP_BLASTER;
				bestweight = 1;
			}
			else if (BotWeaponSelectable(bs, WP_DISRUPTOR))
			{
				bestweapon = WP_DISRUPTOR;
				bestweight = 1;
			}
		}
	}

	//assert(bs->cur_ps.weapon > 0 && bestweapon > 0);

	if (bestweight != -1 && bs->cur_ps.weapon != bestweapon && bs->virtualWeapon != bestweapon)
	{
		bs->virtualWeapon = bestweapon;
		BotSelectWeapon(bs->client, bestweapon);
		//bs->cur_ps.weapon = bestweapon;
		//level.clients[bs->client].ps.weapon = bestweapon;
		return 1;
	}

	//assert(bs->cur_ps.weapon > 0);

	return 0;
}

//check/select the chosen weapon
int BotSelectChoiceWeapon(bot_state_t* bs, int weapon, int doselection)
{ //if !doselection then bot will only check if he has the specified weapon and return 1 (yes) or 0 (no)
	int i;
	int hasit = 0;

	i = 0;

	while (i < WP_NUM_WEAPONS)
	{
		if (bs->cur_ps.ammo[weaponData[i].ammoIndex] > weaponData[i].energyPerShot &&
			i == weapon &&
			(bs->cur_ps.stats[STAT_WEAPONS] & (1 << i)))
		{
			hasit = 1;
			break;
		}

		i++;
	}

	if (hasit && bs->cur_ps.weapon != weapon && doselection && bs->virtualWeapon != weapon)
	{
		bs->virtualWeapon = weapon;
		BotSelectWeapon(bs->client, weapon);
		//bs->cur_ps.weapon = weapon;
		//level.clients[bs->client].ps.weapon = weapon;
		return 2;
	}

	if (hasit)
	{
		return 1;
	}

	return 0;
}

//override our standard weapon choice with a melee weapon
int BotSelectMelee(bot_state_t* bs)
{
	if (bs->cur_ps.weapon != 1 && bs->virtualWeapon != 1)
	{
		bs->virtualWeapon = 1;
		BotSelectWeapon(bs->client, 1);
		//bs->cur_ps.weapon = 1;
		//level.clients[bs->client].ps.weapon = 1;
		return 1;
	}

	return 0;
}

//See if we our in love with the potential bot.
int GetLoveLevel(bot_state_t* bs, bot_state_t* love)
{
	int i = 0;
	const char* lname = NULL;

	if (level.gametype == GT_DUEL || level.gametype == GT_POWERDUEL)
	{ //There is no love in 1-on-1
		return 0;
	}

	if (!bs || !love || !g_entities[love->client].client)
	{
		return 0;
	}

	if (!bs->lovednum)
	{
		return 0;
	}

	if (!bot_attachments.integer)
	{
		return 1;
	}

	lname = g_entities[love->client].client->pers.netname;

	if (!lname)
	{
		return 0;
	}

	while (i < bs->lovednum)
	{
		if (strcmp(bs->loved[i].name, lname) == 0)
		{
			return bs->loved[i].level;
		}

		i++;
	}

	return 0;
}

//Our loved one was killed. We must become infuriated!
void BotLovedOneDied(bot_state_t* bs, bot_state_t* loved, int lovelevel)
{
	if (!loved->lastHurt || !loved->lastHurt->client ||
		loved->lastHurt->s.number == loved->client)
	{
		return;
	}

	if (level.gametype == GT_DUEL || level.gametype == GT_POWERDUEL)
	{ //There is no love in 1-on-1
		return;
	}

	if (!IsTeamplay())
	{
		if (lovelevel < 2)
		{
			return;
		}
	}
	else if (OnSameTeam(&g_entities[bs->client], loved->lastHurt))
	{ //don't hate teammates no matter what
		return;
	}

	if (loved->client == loved->lastHurt->s.number)
	{
		return;
	}

	if (bs->client == loved->lastHurt->s.number)
	{ //oops!
		return;
	}

	if (!bot_attachments.integer)
	{
		return;
	}

	if (!PassLovedOneCheck(bs, loved->lastHurt))
	{ //a loved one killed a loved one.. you cannot hate them
		bs->chatObject = loved->lastHurt;
		bs->chatAltObject = &g_entities[loved->client];
		BotDoChat(bs, "LovedOneKilledLovedOne", 0);
		return;
	}

	if (bs->revengeEnemy == loved->lastHurt)
	{
		if (bs->revengeHateLevel < bs->loved_death_thresh)
		{
			bs->revengeHateLevel++;

			if (bs->revengeHateLevel == bs->loved_death_thresh)
			{
				//broke into the highest anger level
				//CHAT: Hatred section
				bs->chatObject = loved->lastHurt;
				bs->chatAltObject = NULL;
				BotDoChat(bs, "Hatred", 1);
			}
		}
	}
	else if (bs->revengeHateLevel < bs->loved_death_thresh - 1)
	{ //only switch hatred if we don't hate the existing revenge-enemy too much
		//CHAT: BelovedKilled section
		bs->chatObject = &g_entities[loved->client];
		bs->chatAltObject = loved->lastHurt;
		BotDoChat(bs, "BelovedKilled", 0);
		bs->revengeHateLevel = 0;
		bs->revengeEnemy = loved->lastHurt;
	}
}

void BotDeathNotify(bot_state_t* bs)
{ //in case someone has an emotional attachment to us, we'll notify them
	int i = 0;
	int ltest = 0;

	while (i < MAX_CLIENTS)
	{
		if (botstates[i] && botstates[i]->lovednum)
		{
			ltest = 0;
			while (ltest < botstates[i]->lovednum)
			{
				if (strcmp(level.clients[bs->client].pers.netname, botstates[i]->loved[ltest].name) == 0)
				{
					BotLovedOneDied(botstates[i], bs, botstates[i]->loved[ltest].level);
					break;
				}

				ltest++;
			}
		}

		i++;
	}
}

//perform strafe trace checks
void StrafeTracing(bot_state_t* bs)
{
	vec3_t mins, maxs, right, rorg, drorg, forward, up;
	trace_t tr;
	float wallDistanceLeft, wallDistanceRight;

	// REDUCE strafe frequency - Only 30% chance to execute
	if (Q_irand(1, 10) > 3)
	{
		return;
	}

	VectorSet(mins, -15, -15, -22);
	VectorSet(maxs, 15, 15, 32);

	AngleVectors(bs->viewangles, forward, right, up);

	// Check left wall distance
	VectorMA(bs->origin, -32, right, rorg);
	JP_Trace(&tr, bs->origin, mins, maxs, rorg, bs->client, MASK_SOLID, qfalse, 0, 0);
	wallDistanceLeft = tr.fraction * 32;

	// Check right wall distance  
	VectorMA(bs->origin, 32, right, rorg);
	JP_Trace(&tr, bs->origin, mins, maxs, rorg, bs->client, MASK_SOLID, qfalse, 0, 0);
	wallDistanceRight = tr.fraction * 32;

	// If both sides are blocked by walls, don't strafe
	if (wallDistanceLeft < 16 && wallDistanceRight < 16)
	{
		bs->meleeStrafeDisable = level.time + Q_irand(500, 1000);
		return;
	}

	// Prefer strafing away from closest wall
	if (wallDistanceLeft < wallDistanceRight)
	{
		// Wall is closer on left, strafe right
		if (bs->meleeStrafeDir == 0) // if currently trying to go left
		{
			bs->meleeStrafeDir = 1; // switch to right
			bs->meleeStrafeDisable = level.time + Q_irand(100, 300);
		}
	}
	else
	{
		// Wall is closer on right, strafe left
		if (bs->meleeStrafeDir == 1) // if currently trying to go right
		{
			bs->meleeStrafeDir = 0; // switch to left
			bs->meleeStrafeDisable = level.time + Q_irand(100, 300);
		}
	}

	// Original strafe logic with wall awareness
	if (bs->meleeStrafeDir)
	{
		rorg[0] = bs->origin[0] - right[0] * 32;
		rorg[1] = bs->origin[1] - right[1] * 32;
		rorg[2] = bs->origin[2] - right[2] * 32;
	}
	else
	{
		rorg[0] = bs->origin[0] + right[0] * 32;
		rorg[1] = bs->origin[1] + right[1] * 32;
		rorg[2] = bs->origin[2] + right[2] * 32;
	}

	JP_Trace(&tr, bs->origin, mins, maxs, rorg, bs->client, MASK_SOLID, qfalse, 0, 0);

	if (tr.fraction != 1)
	{
		bs->meleeStrafeDisable = level.time + Q_irand(100, 300);
		return;
	}

	VectorCopy(rorg, drorg);
	drorg[2] -= 32;

	JP_Trace(&tr, rorg, NULL, NULL, drorg, bs->client, MASK_SOLID, qfalse, 0, 0);

	if (tr.fraction == 1)
	{
		//this may be a dangerous ledge, so don't strafe over it just in case
		bs->meleeStrafeDisable = level.time + Q_irand(100, 300);
	}
} // Niksata Edit

//doing primary weapon fire
int PrimFiring(bot_state_t* bs)
{
	if (bs->cur_ps.weaponstate != WEAPON_CHARGING &&
		bs->doAttack)
	{
		return 1;
	}

	if (bs->cur_ps.weaponstate == WEAPON_CHARGING &&
		!bs->doAttack)
	{
		return 1;
	}

	return 0;
}

//should we keep our primary weapon from firing?
int KeepPrimFromFiring(bot_state_t* bs)
{
	if (bs->cur_ps.weaponstate != WEAPON_CHARGING &&
		bs->doAttack)
	{
		bs->doAttack = 0;
	}

	if (bs->cur_ps.weaponstate == WEAPON_CHARGING &&
		!bs->doAttack)
	{
		bs->doAttack = 1;
	}

	return 0;
}

//doing secondary weapon fire
int AltFiring(bot_state_t* bs)
{
	if (bs->cur_ps.weaponstate != WEAPON_CHARGING_ALT &&
		bs->doAltAttack)
	{
		return 1;
	}

	if (bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT &&
		!bs->doAltAttack)
	{
		return 1;
	}

	return 0;
}

//should we keep our alt from firing?
int KeepAltFromFiring(bot_state_t* bs)
{
	if (bs->cur_ps.weaponstate != WEAPON_CHARGING_ALT &&
		bs->doAltAttack)
	{
		bs->doAltAttack = 0;
	}

	if (bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT &&
		!bs->doAltAttack)
	{
		bs->doAltAttack = 1;
	}

	return 0;
}

//Try not to shoot our friends in the back. Or in the face. Or anywhere, really.
gentity_t* CheckForFriendInLOF(bot_state_t* bs)
{
	vec3_t fwd;
	vec3_t trfrom, trto;
	vec3_t mins, maxs;
	gentity_t* trent;
	trace_t tr;

	mins[0] = -3;
	mins[1] = -3;
	mins[2] = -3;

	maxs[0] = 3;
	maxs[1] = 3;
	maxs[2] = 3;

	AngleVectors(bs->viewangles, fwd, NULL, NULL);

	VectorCopy(bs->eye, trfrom);

	trto[0] = trfrom[0] + fwd[0] * 2048;
	trto[1] = trfrom[1] + fwd[1] * 2048;
	trto[2] = trfrom[2] + fwd[2] * 2048;

	JP_Trace(&tr, trfrom, mins, maxs, trto, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

	if (tr.fraction != 1 && tr.entityNum <= MAX_CLIENTS)
	{
		trent = &g_entities[tr.entityNum];

		if (trent && trent->client)
		{
			if (IsTeamplay() && OnSameTeam(&g_entities[bs->client], trent))
			{
				return trent;
			}

			if (botstates[trent->s.number] && GetLoveLevel(bs, botstates[trent->s.number]) > 1)
			{
				return trent;
			}
		}
	}

	return NULL;
}

void BotScanForLeader(bot_state_t* bs)
{ //bots will only automatically obtain a leader if it's another bot using this method.
	int i = 0;
	gentity_t* ent;

	if (bs->isSquadLeader)
	{
		return;
	}

	while (i < MAX_CLIENTS)
	{
		ent = &g_entities[i];

		if (ent && ent->client && botstates[i] && botstates[i]->isSquadLeader && bs->client != i)
		{
			if (OnSameTeam(&g_entities[bs->client], ent))
			{
				bs->squadLeader = ent;
				break;
			}
			if (GetLoveLevel(bs, botstates[i]) > 1 && !IsTeamplay())
			{ //ignore love status regarding squad leaders if we're in teamplay
				bs->squadLeader = ent;
				break;
			}
		}

		i++;
	}
}

//w3rd to the p33pz.
void BotReplyGreetings(bot_state_t* bs)
{
	int i = 0;
	int numhello = 0;

	while (i < MAX_CLIENTS)
	{
		if (botstates[i] &&
			botstates[i]->canChat &&
			i != bs->client)
		{
			botstates[i]->chatObject = &g_entities[bs->client];
			botstates[i]->chatAltObject = NULL;
			if (BotDoChat(botstates[i], "ResponseGreetings", 0))
			{
				numhello++;
			}
		}

		if (numhello > 3)
		{ //don't let more than 4 bots say hello at once
			return;
		}

		i++;
	}
}

//try to move in to grab a nearby flag
void CTFFlagMovement(bot_state_t* bs)
{
	int diddrop = 0;
	gentity_t* desiredDrop = NULL;
	vec3_t a, mins, maxs;
	trace_t tr;

	mins[0] = -15;
	mins[1] = -15;
	mins[2] = -7;
	maxs[0] = 15;
	maxs[1] = 15;
	maxs[2] = 7;

	if (bs->wantFlag && (bs->wantFlag->flags & FL_DROPPED_ITEM))
	{
		if (bs->staticFlagSpot[0] == bs->wantFlag->s.pos.trBase[0] &&
			bs->staticFlagSpot[1] == bs->wantFlag->s.pos.trBase[1] &&
			bs->staticFlagSpot[2] == bs->wantFlag->s.pos.trBase[2])
		{
			VectorSubtract(bs->origin, bs->wantFlag->s.pos.trBase, a);

			if (VectorLength(a) <= BOT_FLAG_GET_DISTANCE)
			{
				VectorCopy(bs->wantFlag->s.pos.trBase, bs->goalPosition);
				return;
			}
			else
			{
				bs->wantFlag = NULL;
			}
		}
		else
		{
			bs->wantFlag = NULL;
		}
	}
	else if (bs->wantFlag)
	{
		bs->wantFlag = NULL;
	}

	if (flagRed && flagBlue)
	{
		if (bs->wpDestination == flagRed ||
			bs->wpDestination == flagBlue)
		{
			if (bs->wpDestination == flagRed && droppedRedFlag && (droppedRedFlag->flags & FL_DROPPED_ITEM) && droppedRedFlag->classname && strcmp(droppedRedFlag->classname, "freed") != 0)
			{
				desiredDrop = droppedRedFlag;
				diddrop = 1;
			}
			if (bs->wpDestination == flagBlue && droppedBlueFlag && (droppedBlueFlag->flags & FL_DROPPED_ITEM) && droppedBlueFlag->classname && strcmp(droppedBlueFlag->classname, "freed") != 0)
			{
				desiredDrop = droppedBlueFlag;
				diddrop = 1;
			}

			if (diddrop && desiredDrop)
			{
				VectorSubtract(bs->origin, desiredDrop->s.pos.trBase, a);

				if (VectorLength(a) <= BOT_FLAG_GET_DISTANCE)
				{
					JP_Trace(&tr, bs->origin, mins, maxs, desiredDrop->s.pos.trBase, bs->client, MASK_SOLID, qfalse, 0, 0);

					if (tr.fraction == 1 || tr.entityNum == desiredDrop->s.number)
					{
						VectorCopy(desiredDrop->s.pos.trBase, bs->goalPosition);
						VectorCopy(desiredDrop->s.pos.trBase, bs->staticFlagSpot);
						return;
					}
				}
			}
		}
	}
}

//see if we want to make our detpacks blow up
void BotCheckDetPacks(bot_state_t* bs)
{
	gentity_t* dp = NULL;
	gentity_t* myDet = NULL;
	vec3_t a;
	float enLen;
	float myLen;

	while ((dp = G_Find(dp, FOFS(classname), "detpack")) != NULL)
	{
		if (dp && dp->parent && dp->parent->s.number == bs->client)
		{
			myDet = dp;
			break;
		}
	}

	if (!myDet)
	{
		return;
	}

	if (!bs->currentEnemy || !bs->currentEnemy->client || !bs->frame_Enemy_Vis)
	{ //require the enemy to be visilbe just to be fair..

		//unless..
		if (bs->currentEnemy && bs->currentEnemy->client &&
			(level.time - bs->plantContinue) < 5000)
		{ //it's a fresh plant (within 5 seconds) so we should be able to guess
			goto stillmadeit;
		}
		return;
	}

stillmadeit:

	VectorSubtract(bs->currentEnemy->client->ps.origin, myDet->s.pos.trBase, a);
	enLen = VectorLength(a);

	VectorSubtract(bs->origin, myDet->s.pos.trBase, a);
	myLen = VectorLength(a);

	if (enLen > myLen)
	{
		return;
	}

	if (enLen < BOT_PLANT_BLOW_DISTANCE && OrgVisible(bs->currentEnemy->client->ps.origin, myDet->s.pos.trBase, bs->currentEnemy->s.number))
	{ //we could just call the "blow all my detpacks" function here, but I guess that's cheating.
		bs->plantKillEmAll = level.time + 500;
	}
}

//see if it would be beneficial at this time to use one of our inv items
int BotUseInventoryItem(bot_state_t* bs)
{
	if (bs->cur_ps.stats[STAT_HOLDABLE_ITEMS] & (1 << HI_MEDPAC))
	{
		if (g_entities[bs->client].health <= 75)
		{
			bs->cur_ps.stats[STAT_HOLDABLE_ITEM] = BG_GetItemIndexByTag(HI_MEDPAC, IT_HOLDABLE);
			goto wantuseitem;
		}
	}
	if (bs->cur_ps.stats[STAT_HOLDABLE_ITEMS] & (1 << HI_MEDPAC_BIG))
	{
		if (g_entities[bs->client].health <= 50)
		{
			bs->cur_ps.stats[STAT_HOLDABLE_ITEM] = BG_GetItemIndexByTag(HI_MEDPAC_BIG, IT_HOLDABLE);
			goto wantuseitem;
		}
	}
	if (bs->cur_ps.stats[STAT_HOLDABLE_ITEMS] & (1 << HI_SEEKER))
	{
		if (bs->currentEnemy && bs->frame_Enemy_Vis)
		{
			bs->cur_ps.stats[STAT_HOLDABLE_ITEM] = BG_GetItemIndexByTag(HI_SEEKER, IT_HOLDABLE);
			goto wantuseitem;
		}
	}
	if (bs->cur_ps.stats[STAT_HOLDABLE_ITEMS] & (1 << HI_SENTRY_GUN))
	{
		if (bs->currentEnemy && bs->frame_Enemy_Vis)
		{
			bs->cur_ps.stats[STAT_HOLDABLE_ITEM] = BG_GetItemIndexByTag(HI_SENTRY_GUN, IT_HOLDABLE);
			goto wantuseitem;
		}
	}
	if (bs->cur_ps.stats[STAT_HOLDABLE_ITEMS] & (1 << HI_SHIELD))
	{
		if (bs->currentEnemy && bs->frame_Enemy_Vis && bs->runningToEscapeThreat)
		{ //this will (hopefully) result in the bot placing the shield down while facing
		  //the enemy and running away
			bs->cur_ps.stats[STAT_HOLDABLE_ITEM] = BG_GetItemIndexByTag(HI_SHIELD, IT_HOLDABLE);
			goto wantuseitem;
		}
	}

	return 0;

wantuseitem:
	level.clients[bs->client].ps.stats[STAT_HOLDABLE_ITEM] = bs->cur_ps.stats[STAT_HOLDABLE_ITEM];

	return 1;
}

//trace forward to see if we can plant a detpack or something
int BotSurfaceNear(bot_state_t* bs)
{
	trace_t tr;
	vec3_t fwd;

	AngleVectors(bs->viewangles, fwd, NULL, NULL);

	fwd[0] = bs->origin[0] + (fwd[0] * 64);
	fwd[1] = bs->origin[1] + (fwd[1] * 64);
	fwd[2] = bs->origin[2] + (fwd[2] * 64);

	JP_Trace(&tr, bs->origin, NULL, NULL, fwd, bs->client, MASK_SOLID, qfalse, 0, 0);

	if (tr.fraction != 1)
	{
		return 1;
	}

	return 0;
}

//could we block projectiles from the weapon potentially with a light saber?
int BotWeaponBlockable(int weapon)
{
	switch (weapon)
	{
	case WP_STUN_BATON:
	case WP_MELEE:
		return 0;
	case WP_DISRUPTOR:
		return 0;
	case WP_DEMP2:
		return 0;
	case WP_ROCKET_LAUNCHER:
		return 0;
	case WP_THERMAL:
		return 0;
	case WP_TRIP_MINE:
		return 0;
	case WP_DET_PACK:
		return 0;
	default:
		return 1;
	}
}

void Cmd_EngageDuel_f(gentity_t* ent, int dueltype);
void Cmd_ToggleSaber_f(gentity_t* ent);

//movement overrides
void Bot_SetForcedMovement(int bot, int forward, int right, int up)
{
	bot_state_t* bs;

	bs = botstates[bot];

	if (!bs)
	{ //not a bot
		return;
	}

	if (forward != -1)
	{
		if (bs->forceMove_Forward)
		{
			bs->forceMove_Forward = 0;
		}
		else
		{
			bs->forceMove_Forward = forward;
		}
	}
	if (right != -1)
	{
		if (bs->forceMove_Right)
		{
			bs->forceMove_Right = 0;
		}
		else
		{
			bs->forceMove_Right = right;
		}
	}
	if (up != -1)
	{
		if (bs->forceMove_Up)
		{
			bs->forceMove_Up = 0;
		}
		else
		{
			bs->forceMove_Up = up;
		}
	}
}

void NewBotAI_GetStrafeAim(bot_state_t* bs)
{
	vec3_t headlevel, a, ang;
	float optimalAngle, newAngle = 0, frameTime = 0.008f;
	const float baseSpeed = bs->cur_ps.speed, currentSpeed = sqrt((bs->cur_ps.velocity[0] * bs->cur_ps.velocity[0]) + (bs->cur_ps.velocity[1] * bs->cur_ps.velocity[1]));

	VectorCopy(bs->currentEnemy->client->ps.origin, headlevel);

	if (bs->currentEnemy->client)
		headlevel[2] += bs->currentEnemy->client->ps.viewheight - 24;//aim at chest?

	VectorSubtract(headlevel, bs->eye, a);
	vectoangles(a, ang);
	VectorCopy(ang, bs->goalAngles);

	//Treat angle to them as our base angle.. add offset angle to that to accel?

	optimalAngle = acos((double)((baseSpeed - (baseSpeed * frameTime)) / currentSpeed)) * (180.0f / M_PI) - 45.0f;

	optimalAngle += 1.0f + bot_strafeOffset.value; //Ayy

	if (optimalAngle < 0 || optimalAngle > 360)
		optimalAngle = 0;

	if (bs->forceMove_Forward) {
		if (bs->forceMove_Right < 0)
			newAngle = optimalAngle; //WA
		else if (bs->forceMove_Right > 0)
			newAngle = -optimalAngle; //WD
	}
	else {
		if (bs->forceMove_Right < 0)
			newAngle = 45.0f - optimalAngle;//D
		else if (bs->forceMove_Right > 0)
			newAngle = -(45.0f - optimalAngle);//A
	}

	//trap->Print("Current: %f, Dir: %f, New: %f\n", bs->goalAngles[YAW],  moveAngles[YAW], newAngle);

	bs->goalAngles[YAW] = bs->aimOffsetAmtYaw + newAngle;

	VectorCopy(bs->goalAngles, bs->ideal_viewangles);
	//trap_EA_View(bs->client, bs->goalAngles); // if we want instant aim?
}

void NewBotAI_GetAim(bot_state_t* bs)
{
	vec3_t headlevel;
	int i = 0;
	int closestSaber = 0, saberDistance = 9999999, dist, saberOwner;
	vec3_t saberDiff;
	gentity_t* saber;

	bs->hitSpotted = qfalse;
	if (bs->runningLikeASissy) {
		NewBotAI_GetStrafeAim(bs);
		return;
	}

	if (!bs->currentEnemy || !bs->currentEnemy->client)
		return;

	/*
		trType_t	trType;
	int		trTime;
	int		trDuration;			// if non 0, trTime + trDuration = stop time
	vec3_t	trBase;
	vec3_t	trDelta;			// velocity, etc
	*/
	//Well we should loop through every client and see if they are saberthrowing.  Then get the closest saber to us and aim at that if its close enough.
	if (!g_entities[bs->client].client->ps.saberInFlight) {
		while (i <= MAX_CLIENTS)
		{
			if (i != bs->client && g_entities[i].client && g_entities[i].client->ps.saberInFlight && !OnSameTeam(&g_entities[bs->client], &g_entities[i]) && PassStandardEnemyChecks(bs, &g_entities[i])
				&& PassLovedOneCheck(bs, &g_entities[i]))
			{
				saber = &g_entities[g_entities[i].client->ps.saberEntityNum];

				VectorSubtract(bs->cur_ps.origin, saber->s.pos.trBase, saberDiff);
				dist = VectorLengthSquared(saberDiff);

				if (dist < saberDistance && saber->s.pos.trTime) {
					saberOwner = i;
					closestSaber = g_entities[i].client->ps.saberEntityNum;
					saberDistance = dist;
				}
			}
			i++;
		}
	}
	//Saber is inrange , AND  nearest target is far enough away OR thrower is nearest target(?
	if (saberDistance < 200 * 200 && (bs->frame_Enemy_Len > 200 || bs->currentEnemy->client->ps.clientNum == saberOwner)) { //Dont aim at it if theres a diff enemy in saber range?
		vec3_t a, ang;
		saber = &g_entities[closestSaber];

		//VectorCopy(saber->s.pos.trBase, headlevel);
		//BotAimLeading(bs, headlevel, bLeadAmount);

		if (saber) {
			VectorSubtract(saber->s.pos.trBase, bs->eye, a);
			vectoangles(a, ang);
			VectorCopy(ang, bs->goalAngles);
			bs->hitSpotted = qtrue;
		}
	}
	/*
	if (bs->cur_ps.weapon == WP_SABER && bs->currentEnemy->client->ps.saberInFlight && bs->frame_Enemy_Len > 200) { //Try to block saber in air
		//Go through each entity, check if saber and if owner is currentenemY? if yes aim at it..?

		gentity_t *saber = &g_entities[bs->currentEnemy->client->ps.saberEntityNum];
		VectorCopy(saber->s.pos.trBase, headlevel);

		BotAimLeading(bs, headlevel, bLeadAmount);

		g_entities[bs->client].client->pers.JAWARUN = qtrue;


	} */

	else { //Normal aim at player
		VectorCopy(bs->currentEnemy->client->ps.origin, headlevel);

		if (bs->currentEnemy && bs->currentEnemy->client)
			headlevel[2] += bs->currentEnemy->client->ps.viewheight - 16;//aim at chest?
		if (bs->cur_ps.saberInFlight)
			headlevel[2] += 24; //aim a bit higher for saberthrow
		G_NewBotAIAimLeading(bs, headlevel);
	}
	VectorCopy(bs->goalAngles, bs->ideal_viewangles);
}

float NewBotAI_GetDist(bot_state_t* bs)
{
	if (bs->currentEnemy)
	{
		vec3_t diff, eorg;
		VectorCopy(bs->currentEnemy->client->ps.origin, eorg);
		VectorSubtract(eorg, bs->eye, diff);
		return VectorLength(diff);
	}
	return 0;
}

//Milliseconds until 2 moving points are within X distance of eachother
int NewBotAI_GetTimeToInRange(bot_state_t* bs, int range, int maxTime) {
	//Get velocity of bot relative to player
	//Dotproduct that velocity with the position difference between players

	//or simulate it
	float tick = 1000 / sv_fps.integer;
	int timeToInRange = 0;
	int i;
	int attempts = sv_fps.integer * maxTime * 0.001f;
	int remainder;
	vec3_t diff, pos1, pos2, vel1, vel2;

	VectorCopy(bs->cur_ps.origin, pos1);
	VectorCopy(bs->currentEnemy->client->ps.origin, pos2);
	VectorCopy(bs->cur_ps.velocity, vel1);
	VectorCopy(bs->currentEnemy->client->ps.velocity, vel2);
	range = range * range; //for vectorlength squared

	remainder = maxTime % (int)tick;
	timeToInRange += remainder;

	vel1[0] /= tick;
	vel1[1] /= tick;
	vel1[2] /= tick;

	vel2[0] /= tick;
	vel2[1] /= tick;
	vel2[2] /= tick;

	for (i = 0; i < attempts; i++) { //Check up to 1 second in future?
		VectorSubtract(pos1, pos2, diff);

		VectorAdd(pos1, vel1, pos1);
		VectorAdd(pos2, vel2, pos2);

		if (VectorLengthSquared(diff) < range) {
			break;
		}

		timeToInRange += tick;
	}

	//Com_Printf("TTR is %i\n", timeToInRange);
	return timeToInRange; //If this is less than maxTime, we will be in range!

}
qboolean BG_SaberInAttack(int move);
qboolean BG_InKnockDown(int anim);
int NewBotAI_GetAbsorb(bot_state_t* bs) {
	const int ourForce = bs->cur_ps.fd.forcePower;

	if (g_forcePowerDisable.integer & (1 << FP_ABSORB))
		return 0;
	if (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_ABSORB)))
		return 0;
	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_ABSORB))
		return 0;
	if (ourForce < 12)
		return 0;
	if (!bs->frame_Enemy_Vis) //Only absorb if LOS
		return 0;
	if (bs->frame_Enemy_Len > MAX_DRAIN_DISTANCE)
		return 0;

	if (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_DRAIN) || bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_LIGHTNING)) //check if they are being hit though. not if enemy is using it?
		return 100;

	if (bs->cur_ps.weapon > WP_BRYAR_PISTOL && (bs->frame_Enemy_Len < 200)) { //Protect our guns
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_ABSORB;
		return 75; //eh?
	}

	if (bs->currentEnemy->client->ps.fd.forcePower >= 20 && g_entities[bs->client].health < 30) {//PK range? and he can be pulled? and low
		if (bs->frame_Enemy_Len > 50) { //no point in absorbing if we are touching them anyway
			if (bs->frame_Enemy_Len < 256 && (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE || BG_SaberInAttack(bs->cur_ps.saberMove) || BG_InKnockDown(bs->cur_ps.legsAnim)))//can actually be pulled towards a kick so stop that
				return ourForce * 0.5f - 10;
		}
	}

	return 0;
}

int NewBotAI_GetProtect(bot_state_t* bs) {
	const int ourForce = bs->cur_ps.fd.forcePower;
	const int totalhealth = g_entities[bs->client].health + bs->currentEnemy->client->ps.stats[STAT_ARMOR];
	//Only toggle protect on if we are about to get damaged..

	if (g_forcePowerDisable.integer & (1 << FP_PROTECT))
		return 0;
	if (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_PROTECT)))
		return 0;
	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_PROTECT))
		return 0;
	if (ourForce < 12)
		return 0;

	if (bs->hitSpotted && totalhealth > 6 && (BG_SaberInAttack(bs->cur_ps.saberMove) || BG_InKnockDown(bs->cur_ps.legsAnim))) { //About to be saberthrowed and we can survive it and we can't block it
		if (totalhealth > 35) { //we could survive it anyway
			return (ourForce - 15);
		}
		return 100;
	}

	if (bs->frame_Enemy_Len < 120 && BG_SaberInAttack(bs->currentEnemy->client->ps.saberMove)) { //better way to check if we are about to take damage?
		vec3_t diff;

		VectorSubtract(bs->cur_ps.origin, bs->currentEnemy->client->saber[0].blade[0].trail.tip, diff);
		if (VectorLengthSquared(diff) > (48 * 48)) //out of range
			return (ourForce * 0.5f) - 10;
	}

	//Get nearest gun.. if (bs->currentEnemy->client->ps.weapon != WP_SABER)?  trace nearby projectiles? or is that not quick enough reaction time
	//Weigh differently if we already have absorb ?
	return 0;
}

void NewBotAI_Getup(bot_state_t* bs)
{
	qboolean useTheForce = qfalse;

	trap->EA_Jump(bs->client);

	if ((bs->cur_ps.fd.forceGripBeingGripped > level.time) && bs->cur_ps.velocity[2] < -100) { //in grip and going down, a splat?
		if (bs->cur_ps.fd.forcePowersKnown & (1 << FP_PROTECT)) {
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_PROTECT;
			useTheForce = qtrue;
		}
		/*if (bs->cur_ps.fd.forcePowersKnown & (1 << FP_RAGE)) {
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_RAGE;
			useTheForce = qtrue;
		}*/
	}

	if (!useTheForce && NewBotAI_GetAbsorb(bs)) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_ABSORB;
		useTheForce = qtrue;
	}
	if (!useTheForce && NewBotAI_GetProtect(bs)) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_PROTECT;
		useTheForce = qtrue;
	}

	if ((!useTheForce && ((bs->frame_Enemy_Len < 200) || (bs->cur_ps.fd.forceGripBeingGripped > level.time)))) {
		if (!(g_forcePowerDisable.integer & (1 << FP_PUSH)) && bs->cur_ps.fd.forcePowersKnown & (1 << FP_PUSH)) {
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_PUSH;
			useTheForce = qtrue;
		}
	}

	if (useTheForce) {
		trap->EA_ForcePower(bs->client);
	}
}

void NewBotAI_Flipkick(bot_state_t* bs)
{
	qboolean enemySwing = qfalse;
	if (bs->currentEnemy && bs->currentEnemy->client && bs->currentEnemy->client->ps.weapon == WP_SABER && bs->cur_ps.torsoTimer) {
		enemySwing = qtrue;
	}

	if (bs->currentEnemy && bs->currentEnemy->client && bs->cur_ps.saberMove == LS_A_T2B && (bs->currentEnemy->client->ps.saberMove != LS_READY || bs->currentEnemy->client->ps.weapon != WP_SABER || bs->currentEnemy->client->ps.fd.saberAnimLevel != SS_STRONG) && bs->frame_Enemy_Len < 180 && !enemySwing) {//In range and they can't block it
		if (bs->cur_ps.torsoTimer < 350 && bs->cur_ps.torsoTimer > 100 && bs->frame_Enemy_Len < 130) {
			return;
		}
	}

	if (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE - 1 || bs->cur_ps.fd.forceJumpZStart < 16) {//idk
		trap->EA_MoveForward(bs->client);
		trap->EA_Jump(bs->client);
	}

	//if red swing and during the good part of anim and they are in range of saber dont kick them.. yet
	if (bs->currentEnemy && bs->currentEnemy->client && bs->cur_ps.saberMove == LS_A_T2B && (bs->currentEnemy->client->ps.saberMove != LS_READY || bs->currentEnemy->client->ps.weapon != WP_SABER || bs->currentEnemy->client->ps.fd.saberAnimLevel != SS_STRONG) && bs->frame_Enemy_Len < 180 && !enemySwing) {//In range and they can't block it
		if (bs->cur_ps.torsoTimer < 250 && bs->cur_ps.torsoTimer > 100 && bs->frame_Enemy_Len < 110) {
			trap->EA_Crouch(bs->client);
			return;
		}
	}

	else if (((bs->origin[2] - bs->cur_ps.fd.forceJumpZStart) > 24) && ((bs->origin[2] - bs->cur_ps.fd.forceJumpZStart) < 48))
	{
		if (level.framenum % 2)
			trap->EA_Jump(bs->client);
	}
}

void NewBotAI_ReactToBeingGripped(bot_state_t* bs) //Test this more, does it push?  wtf?
{
	vec3_t a_fo;
	qboolean useTheForce = qfalse;

	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, a_fo);
	vectoangles(a_fo, a_fo);

	if (!(g_forcePowerDisable.integer & (1 << FP_ABSORB)) && bs->cur_ps.fd.forcePowersKnown & (1 << FP_ABSORB)) {//Can pull and not push
		if (bs->cur_ps.fd.forcePower >= 12) {
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_ABSORB;
			useTheForce = qtrue;
		}
	}
	else if (!(g_forcePowerDisable.integer & (1 << FP_PULL)) && !(g_forcePowerDisable.integer & (1 << FP_PUSH)) && (bs->cur_ps.fd.forcePowersKnown & (1 << FP_PULL)) && (bs->cur_ps.fd.forcePowersKnown & (1 << FP_PUSH))) {//Can push or pull
		if (bs->cur_ps.fd.forcePower >= 20 && InFieldOfVision(bs->viewangles, 50, a_fo)) {
			if (g_entities[bs->client].health < 30) {
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_PUSH;
				useTheForce = qtrue;
			}
			else {
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_PULL;
				useTheForce = qtrue;
			}
		}
	}
	else if (!(g_forcePowerDisable.integer & (1 << FP_PULL)) && bs->cur_ps.fd.forcePowersKnown & (1 << FP_PULL)) {//Can pull and not push
		if (bs->cur_ps.fd.forcePower >= 20 && InFieldOfVision(bs->viewangles, 50, a_fo)) {
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_PULL;
			useTheForce = qtrue;
		}
	}
	else if (!(g_forcePowerDisable.integer & (1 << FP_PULL)) && bs->cur_ps.fd.forcePowersKnown & (1 << FP_PUSH)) {//Can push and not pull
		if (bs->cur_ps.fd.forcePower >= 20 && InFieldOfVision(bs->viewangles, 50, a_fo)) {
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_PUSH;
			useTheForce = qtrue;
		}
	}
	else if (bs->cur_ps.fd.forcePowersActive & (1 << FP_SPEED)) { // we are speeding
		if (bs->cur_ps.fd.forcePowersKnown & (1 << FP_SPEED)) {//make sure..
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_SPEED; //lets turn off speed
			useTheForce = qtrue;
		}
	}

	//if (bs->cur_ps.fd.forcePower >= 20 && InFieldOfVision(bs->viewangles, 50, a_fo)) {
	//	level.clients[bs->client].ps.fd.forcePowerSelected = FP_PUSH;
	//	useTheForce = qtrue;
	//}

	NewBotAI_Flipkick(bs);

	if (useTheForce && (level.framenum % 2)) {
		trap->EA_ForcePower(bs->client);
	}
}

void NewBotAI_Gripkick(bot_state_t* bs)
{
	//float heightDiff = bs->cur_ps.origin[2] - bs->currentEnemy->client->ps.origin[2]; //We are above them by this much
	int gripTime = level.time - bs->currentEnemy->client->ps.fd.forceGripStarted; //Milliseconds we have been gripping them

	if (BG_InKnockDown(bs->currentEnemy->client->ps.legsAnim)) { //Splat and enough time? - how to see if they are splattable and were not gripped during midair. forcejumpzheight ?
		float heightdiff = bs->currentEnemy->client->ps.origin[2] - bs->eye[2]; //Them minus ours,  they are 500, we are 300. height diff is 200.

		if (heightdiff < 110) {
			bs->ideal_viewangles[PITCH] = -90; //60?
		}
		else {
			bs->ideal_viewangles[PITCH] = 90; //-80
		}
		if (bs->currentEnemy->client->ps.velocity[2] < -400) {//going fast enough to die, let go
			return;
		}
		//not a good splat, has to predict based on their momentum
	}
	else {
		if (gripTime < 1000) { //[0-1] second in the grip (Aim down until in range, kick)
			bs->ideal_viewangles[PITCH] = 60; //60?
			trap->EA_MoveForward(bs->client);

		}
		else if (gripTime < 2000) { //[1-2] seconds in the grip (Aim up, spin)
			bs->ideal_viewangles[PITCH] = -80; //-80

			if (level.time % 10000 > 5000) {
				trap->EA_MoveRight(bs->client);
				bs->ideal_viewangles[YAW] += 80; //80?
			}
			else {
				trap->EA_MoveLeft(bs->client);
				bs->ideal_viewangles[YAW] -= 80;//80?
			}
		}
		else if (gripTime < 2200) { //[2-2.2] seconds in the grip (Aim at center for a split second, so they dont get stuck on our head)
			bs->ideal_viewangles[PITCH] = 0;
		}
		else if (gripTime < 4000) { //[2.2-4] seconds in the grip (Aim down until in range, kick)
			bs->ideal_viewangles[PITCH] = -70; //-70?
			trap->EA_MoveForward(bs->client);
		}

		if ((bs->cur_ps.groundEntityNum == ENTITYNUM_NONE - 1) && NewBotAI_GetTimeToInRange(bs, 40, 100) < 100) { //Start a kick if we are in range and on ground
			NewBotAI_Flipkick(bs);
		}
		else if (bs->cur_ps.groundEntityNum != ENTITYNUM_NONE - 1) { //Always try to flipkick while we are in air
			NewBotAI_Flipkick(bs);
		}
	}

	bs->ideal_viewangles[YAW] = AngleNormalize360(bs->ideal_viewangles[YAW]); //Normalize the angles
	bs->ideal_viewangles[PITCH] = AngleNormalize360(bs->ideal_viewangles[PITCH]);

	trap->EA_ForcePower(bs->client); //Always hold grip key during grip
}

void NewBotAI_Lightning(bot_state_t* bs) // Niksata Edit
{
	if (!bs->currentEnemy || !bs->currentEnemy->client)
		return;

	if (bs->frame_Enemy_Len > 1500)
		return;

	if (bs->cur_ps.fd.forcePower < 25)
		return;

	if (bs->currentEnemy->client->ps.fd.forcePower <= 0)
		return;

	if (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB))
		return;

	if (!OrgVisible(bs->eye, bs->currentEnemy->client->ps.origin, bs->client))
		return;

	// Don't use lightning during saber attacks
	if (bs->currentEnemy->client->ps.saberMove > 1)
		return;

	level.clients[bs->client].ps.fd.forcePowerSelected = FP_LIGHTNING;

	// Aim at the enemy with small random offsets for realism
	{
		vec3_t a_fo;
		VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, a_fo);
		vectoangles(a_fo, a_fo);

		a_fo[YAW] += Q_irand(-6, 6);
		a_fo[PITCH] += Q_irand(-4, 4);

		bs->ideal_viewangles[YAW] = AngleNormalize360(a_fo[YAW]);
		bs->ideal_viewangles[PITCH] = AngleNormalize360(a_fo[PITCH]);
	}

	if (bs->frame_Enemy_Len > 280) trap->EA_MoveForward(bs->client);
	else if (bs->frame_Enemy_Len < 180) trap->EA_MoveBack(bs->client);

	// Hold Lightning continuously
	trap->EA_ForcePower(bs->client);
} // Niksata Edit

void NewBotAI_Draining(bot_state_t* bs)
{
	//level.clients[bs->client].ps.fd.forcePowerSelected = FP_GRIP;
	//useTheForce = qtrue;
	//}

	if (((g_entities[bs->client].health) < 100 && bs->currentEnemy->client->ps.fd.forcePower && !(bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB)) && OrgVisible(bs->eye, bs->currentEnemy->client->ps.origin, bs->client)))
		trap->EA_ForcePower(bs->client);
}

void NewBotAI_MindTrick(bot_state_t* bs) // Niksata Edit
{
	if (bs->currentEnemy &&
		g_entities[bs->client].health <= 50 && // Niksata Edit
		bs->currentEnemy->client &&
		bs->currentEnemy->client->ps.fd.forcePower > 0 &&
		!(bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB)) &&
		OrgVisible(bs->eye, bs->currentEnemy->client->ps.origin, bs->client))
	{
		if (bs->cur_ps.fd.forcePower > 25)
		{
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_TELEPATHY;
			trap->EA_ForcePower(bs->client);

		}
	}
} // Niksata Edit

void NewBotAI_Speeding(bot_state_t* bs)
{
	if ((g_entities[bs->client].health) < 90 || (bs->cur_ps.fd.forcePower <= 100)) { // Niksata Edit
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_SPEED;
		trap->EA_ForcePower(bs->client);
	}
}

void NewBotAI_Raging(bot_state_t* bs) // Niksata Edit
{
	const int ourHealth = g_entities[bs->client].health;

	// Use the authoritative ps (not just bs->cur_ps snapshot)
	const int ourFP = level.clients[bs->client].ps.fd.forcePower;

	const qboolean rageActive = (level.clients[bs->client].ps.fd.forcePowersActive & (1 << FP_RAGE)) ? qtrue : qfalse;

	// -----------------------
	// TURN OFF CONDITIONS
	// -----------------------
	if (rageActive)
	{
		if (ourFP < 10)
		{
			// Try normal toggle off first
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_RAGE;
			trap->EA_ForcePower(bs->client);

			// If the engine refuses to process the toggle due to low FP,
			// force-clear it to guarantee it shuts off.
			level.clients[bs->client].ps.fd.forcePowersActive &= ~(1 << FP_RAGE);
			return;
		}

		// Optional: also cancel Rage if you're no longer in the "desperate" health window
		if (ourHealth >= 50)
		{
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_RAGE;
			trap->EA_ForcePower(bs->client);
			return;
		}
	}

	// -----------------------
	// TURN ON CONDITIONS
	// -----------------------
	if (!bs->currentEnemy || !bs->currentEnemy->client)
		return;
	if (!bs->frame_Enemy_Vis)
		return;

	if (bs->frame_Enemy_Len <= 250 &&
		ourHealth < 50 &&
		bs->cur_ps.weapon == WP_SABER &&
		ourFP == 100)
	{
		if (!rageActive)
		{
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_RAGE;
			trap->EA_ForcePower(bs->client);
		}
	}
} // Niksata Edit

void NewBotAI_Protecting(bot_state_t* bs)
{
	qboolean stopProtecting = qfalse;

	//Don't protect if they are out of range
	if (!bs->frame_Enemy_Vis)
		stopProtecting = qtrue;
	else if (bs->frame_Enemy_Len > 512) {
		if (bs->cur_ps.fd.forcePowersActive & (1 << FP_ABSORB)) {
			if ((bs->cur_ps.fd.forcePower < 30))
				stopProtecting = qtrue;
		}
		else {
			if ((bs->cur_ps.fd.forcePower < 40))
				stopProtecting = qtrue;
		}
	}

	if (!bs->currentEnemy->client->ps.saberEntityNum) { //They have a dropped saber = no regen
		if (bs->currentEnemy->client->ps.fd.forcePower < 35)
			stopProtecting = qtrue;
	}

	if (bs->frame_Enemy_Len > 1024)
		stopProtecting = qtrue;

	if (stopProtecting) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_PROTECT;
		trap->EA_ForcePower(bs->client);
	}
}

void NewBotAI_Absorbing(bot_state_t* bs)
{
	qboolean stopAbsorbing = qfalse;
	if (!bs->frame_Enemy_Vis)
		stopAbsorbing = qtrue;
	else if (NewBotAI_GetDist(bs) > MAX_DRAIN_DISTANCE || ((bs->currentEnemy->client->ps.fd.forcePower < 20) && (!(bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_DRAIN))))) {
		if ((bs->cur_ps.fd.forceGripBeingGripped < level.time)) //Not being gripped
			stopAbsorbing = qtrue;
	}

	if (!bs->currentEnemy->client->ps.saberEntityNum) { //They have a dropped saber = no regen
		if (bs->currentEnemy->client->ps.fd.forcePower < 35)
			stopAbsorbing = qtrue;
	}

	if (stopAbsorbing) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_ABSORB;
		trap->EA_ForcePower(bs->client);
	}
}

void NewBotAI_Healing(bot_state_t* bs) // Niksata Edit
{
	// Only heal if health is below 75%
	if (g_entities[bs->client].health < 75)
	{
		// Only heal if enough Force
		if (bs->cur_ps.fd.forcePower > 30 &&
			!(bs->cur_ps.fd.forcePowersActive & (1 << FP_HEAL)))
		{
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_HEAL;
			trap->EA_ForcePower(bs->client);
		}
	}
} // Niksata Edit

void NewBotAI_SaberThrowing(bot_state_t* bs) // Niksata Edit
{
	// NEW: Range check (500-900 units)
	int dist = bs->frame_Enemy_Len;
	const int MIN_THROW_RANGE = 500;
	const int MAX_THROW_RANGE = 2500;

	// Only execute saber throw if in valid range
	if (dist >= MIN_THROW_RANGE && dist <= MAX_THROW_RANGE) {
		trap->EA_Alt_Attack(bs->client);
	}
} // Niksata Edit

float BS_GroundDistance(bot_state_t* bs)
{
	return (bs->origin[2] - bs->cur_ps.fd.forceJumpZStart);
}

float NewBotAI_GetSpeedTowardsEnemy(bot_state_t* bs)
{
	if (bs->currentEnemy)
	{
		float dot;
		qboolean n = qfalse;
		vec3_t diff, eorg;

		VectorCopy(bs->currentEnemy->client->ps.origin, eorg);
		VectorSubtract(eorg, bs->eye, diff);
		dot = DotProduct(diff, bs->cur_ps.velocity); //Should take into account enemy velocity too?
		if (dot < 0) {
			dot = -dot;
			n = qtrue;
		}
		dot = sqrt(dot);
		if (n)
			dot = -dot;
		return dot;//Should we cancel the Z axis on this?
	}
	return 0;
}


int NewBotAI_GetTribesWeapon(bot_state_t* bs)
{
	const int /*hisHealth = bs->currentEnemy->health,*/ distance = bs->frame_Enemy_Len;
	//	int hisWeapon = WP_SABER;
	int bestWeapon = bs->cur_ps.weapon;
	const int forcedFireMode = level.clients[bs->client].forcedFireMode;

	bs->doAltAttack = 0;

	//	if (bs->currentEnemy->client)
	//		hisWeapon = bs->currentEnemy->client->ps.weapon;

		//Dependant on distance from enemy, enemys health, enemys weapon, and our health?


	if (distance > 3000) {
		if (BotWeaponSelectable(bs, WP_DISRUPTOR))
			bestWeapon = WP_DISRUPTOR;
		else if (BotWeaponSelectable(bs, WP_BLASTER) && ((bs->cur_ps.weapon != WP_BLASTER && bs->cur_ps.jetpackFuel == 100) || (bs->cur_ps.weapon == WP_BLASTER && bs->cur_ps.jetpackFuel > 10))) {
			bestWeapon = WP_BLASTER;
		}
		else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_OLD) && ((bs->cur_ps.weapon == WP_BRYAR_OLD && bs->cur_ps.jetpackFuel == 100) || (bs->cur_ps.weapon == WP_BRYAR_OLD && bs->cur_ps.jetpackFuel > 10))) { //logic to let us run down to 0 but nto switch to 0
			bestWeapon = WP_BRYAR_OLD;
			bs->doAltAttack = 1;
			bs->altChargeTime = 800;
		}
		else if (BotWeaponSelectable(bs, WP_REPEATER) && forcedFireMode != 2)
			bestWeapon = WP_REPEATER;
		else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 1) {
			bestWeapon = WP_DEMP2;
			bs->doAltAttack = 1;
			bs->altChargeTime = 2100;
		}
		else if (BotWeaponSelectable(bs, WP_CONCUSSION) && bs->cur_ps.fd.forcePower > 90 && forcedFireMode != 1) {
			bestWeapon = WP_CONCUSSION;
			bs->doAltAttack = 1;
		}
		else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 2) {
			bestWeapon = WP_CONCUSSION;
		}
		else if (BotWeaponSelectable(bs, WP_ROCKET_LAUNCHER) && forcedFireMode != 2) {
			bestWeapon = WP_ROCKET_LAUNCHER;
		}
		else if (BotWeaponSelectable(bs, WP_FLECHETTE)) {
			bestWeapon = WP_FLECHETTE;
			bs->doAltAttack = 1;
		}
		else if (bs->cur_ps.stats[STAT_WEAPONS] & (1 << WP_SABER))
			bestWeapon = WP_SABER;
	}
	else if (distance > 800 && distance < 2800) { //Have some padding between distance tiers so we dont weaponswitch spam
		if (BotWeaponSelectableAltFire(bs, WP_BLASTER) && ((bs->cur_ps.weapon != WP_BLASTER && bs->cur_ps.jetpackFuel == 100) || (bs->cur_ps.weapon == WP_BLASTER && bs->cur_ps.jetpackFuel > 10))) {
			bestWeapon = WP_BLASTER;
		}
		else if (BotWeaponSelectable(bs, WP_REPEATER) && forcedFireMode != 2)
			bestWeapon = WP_REPEATER;
		else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_OLD) && ((bs->cur_ps.weapon == WP_BRYAR_OLD && bs->cur_ps.jetpackFuel == 100) || (bs->cur_ps.weapon == WP_BRYAR_OLD && bs->cur_ps.jetpackFuel > 10))) {
			bestWeapon = WP_BRYAR_OLD;
			bs->doAltAttack = 1;
			bs->altChargeTime = 800;
		}
		else if (BotWeaponSelectableAltFire(bs, WP_BOWCASTER)) {
			bestWeapon = WP_BOWCASTER;
			bs->doAltAttack = 1;
		}
		else if (BotWeaponSelectable(bs, WP_CONCUSSION) && bs->cur_ps.fd.forcePower > 90 && forcedFireMode != 1) {
			bestWeapon = WP_CONCUSSION;
			bs->doAltAttack = 1;
		}
		else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 2) {
			bestWeapon = WP_CONCUSSION;
		}
		else if (BotWeaponSelectable(bs, WP_ROCKET_LAUNCHER) && forcedFireMode != 2) {
			bestWeapon = WP_ROCKET_LAUNCHER;
		}
		else if (BotWeaponSelectable(bs, WP_DISRUPTOR))
			bestWeapon = WP_DISRUPTOR;
		else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 2) {
			bestWeapon = WP_DEMP2;
		}
		else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 1) {
			bestWeapon = WP_DEMP2;
			bs->doAltAttack = 1;
			bs->altChargeTime = 2100;
		}
		else if (BotWeaponSelectable(bs, WP_FLECHETTE)) {
			bestWeapon = WP_FLECHETTE;
			bs->doAltAttack = 1;
		}
		else if (bs->cur_ps.stats[STAT_WEAPONS] & (1 << WP_SABER))
			bestWeapon = WP_SABER;
	}
	else if (distance < 600) { //Most DPS!
		if (BotWeaponSelectableAltFire(bs, WP_THERMAL) && (bs->currentEnemy->client->ps.powerups[PW_REDFLAG] || bs->currentEnemy->client->ps.powerups[PW_BLUEFLAG] || bs->currentEnemy->client->ps.powerups[PW_NEUTRALFLAG])) {
			bestWeapon = WP_THERMAL;
		}
		if (BotWeaponSelectableAltFire(bs, WP_BLASTER) && ((bs->cur_ps.weapon == WP_BLASTER && bs->cur_ps.jetpackFuel == 100) || (bs->cur_ps.weapon == WP_BLASTER && bs->cur_ps.jetpackFuel > 10))) {
			bestWeapon = WP_BLASTER;
			bs->doAltAttack = 1;
		}
		else if (BotWeaponSelectable(bs, WP_FLECHETTE))
			bestWeapon = WP_FLECHETTE;
		else if (BotWeaponSelectable(bs, WP_ROCKET_LAUNCHER))
			bestWeapon = WP_ROCKET_LAUNCHER;
		else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 2)
			bestWeapon = WP_CONCUSSION;
		else if (BotWeaponSelectableAltFire(bs, WP_BOWCASTER)) {
			bestWeapon = WP_BOWCASTER;
			bs->doAltAttack = 1;
		}
		else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 2) {
			bestWeapon = WP_DEMP2;
		}
		else if (BotWeaponSelectable(bs, WP_DISRUPTOR))
			bestWeapon = WP_DISRUPTOR;
		else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_OLD)) {
			bestWeapon = WP_BRYAR_OLD;
			bs->doAltAttack = 1;
			bs->altChargeTime = 800;
		}
		else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_PISTOL)) {
			bestWeapon = WP_BRYAR_PISTOL;
			bs->doAltAttack = 1;
			bs->altChargeTime = 1200;
		}
		else if (BotWeaponSelectable(bs, WP_CONCUSSION) && bs->cur_ps.fd.forcePower > 90 && forcedFireMode != 1) {
			bestWeapon = WP_CONCUSSION;
			bs->doAltAttack = 1;
		}
		else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 1) {
			bestWeapon = WP_DEMP2;
			bs->doAltAttack = 1;
			bs->altChargeTime = 2100;
		}
		else if (bs->cur_ps.stats[STAT_WEAPONS] & (1 << WP_SABER))
			bestWeapon = WP_SABER;
	}


	if (bs->currentEnemy->client && bs->currentEnemy->client->ps.weapon == WP_DEMP2) //dont charge if they can cancel it
		bs->altChargeTime = 50;

	if (forcedFireMode == 1)
		bs->doAltAttack = 0;
	else if (forcedFireMode == 2)
		bs->doAltAttack = 1;

	//todo- weapon table.

	return bestWeapon;
}

int NewBotAI_GetWeapon(bot_state_t* bs)
{
	const int /*hisHealth = bs->currentEnemy->health,*/ distance = bs->frame_Enemy_Len;
	int hisWeapon = WP_SABER;
	int bestWeapon = bs->cur_ps.weapon;
	const int forcedFireMode = level.clients[bs->client].forcedFireMode;

	bs->doAltAttack = 0;

	if (bs->currentEnemy->client)
		hisWeapon = bs->currentEnemy->client->ps.weapon;

	//Dependant on distance from enemy, enemys health, enemys weapon, and our health?

	if (hisWeapon == WP_SABER) { //Use splash damage if possible
		if (distance > 1300) {
			if (BotWeaponSelectable(bs, WP_DISRUPTOR))
				bestWeapon = WP_DISRUPTOR;
			else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 1) {
				bestWeapon = WP_CONCUSSION;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 1) {
				bestWeapon = WP_DEMP2;
				bs->doAltAttack = 1;
				bs->altChargeTime = 2100;
			}
			else if (BotWeaponSelectable(bs, WP_BLASTER)) {
				bestWeapon = WP_BLASTER;
			}
			else if (distance > 500 && BotWeaponSelectableAltFire(bs, WP_BRYAR_OLD)) {
				bestWeapon = WP_BRYAR_OLD;
				bs->doAltAttack = 1;
				bs->altChargeTime = 800;
			}
			else if (distance > 500 && BotWeaponSelectableAltFire(bs, WP_BRYAR_PISTOL)) {
				bestWeapon = WP_BRYAR_PISTOL;
				bs->doAltAttack = 1;
				bs->altChargeTime = 1200;
			}
			else if (bs->cur_ps.stats[STAT_WEAPONS] & (1 << WP_SABER))
				bestWeapon = WP_SABER;
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_OLD)) {
				bestWeapon = WP_BRYAR_OLD;
				bs->doAltAttack = 1;
				bs->altChargeTime = 800;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_PISTOL)) {
				bestWeapon = WP_BRYAR_PISTOL;
				bs->doAltAttack = 1;
				bs->altChargeTime = 1200;
			}
		}
		else if (distance > 350 && distance < 1100) { //Have some padding between distance tiers so we dont weaponswitch spam
			if (distance < 900 && BotWeaponSelectableAltFire(bs, WP_REPEATER) && forcedFireMode != 1) {
				bestWeapon = WP_REPEATER;
				bs->doAltAttack = 1;
			}
			else if (distance < 768 && BotWeaponSelectableAltFire(bs, WP_STUN_BATON) && (g_tweakWeapons.integer & WT_STUN_LG) && !(g_tweakWeapons.integer & WT_STUN_HEAL)) {
				bestWeapon = WP_STUN_BATON;
			}
			else if (BotWeaponSelectable(bs, WP_DISRUPTOR))
				bestWeapon = WP_DISRUPTOR;
			else if (BotWeaponSelectableAltFire(bs, WP_BLASTER)) {
				bestWeapon = WP_BLASTER;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_BOWCASTER)) {
				bestWeapon = WP_BOWCASTER;
				bs->doAltAttack = 1;
			}
			else if (distance > 600 && BotWeaponSelectableAltFire(bs, WP_BRYAR_OLD)) {
				bestWeapon = WP_BRYAR_OLD;
				bs->doAltAttack = 1;
				bs->altChargeTime = 800;
			}
			else if (distance > 700 && BotWeaponSelectableAltFire(bs, WP_BRYAR_PISTOL)) {
				bestWeapon = WP_BRYAR_PISTOL;
				bs->doAltAttack = 1;
				bs->altChargeTime = 1200;
			}
			else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 1) {
				bestWeapon = WP_CONCUSSION;
				bs->doAltAttack = 1;
			}
			else if (bs->cur_ps.stats[STAT_WEAPONS] & (1 << WP_SABER))
				bestWeapon = WP_SABER;
			else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 1) {
				bestWeapon = WP_DEMP2;
				bs->doAltAttack = 1;
				bs->altChargeTime = 2100;
			}
		}
		else if (distance < 200) {
			if (BotWeaponSelectableAltFire(bs, WP_FLECHETTE)) {
				bestWeapon = WP_FLECHETTE;
				if (!(g_tweakWeapons.integer & WT_STAKE_GUN))
					bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectable(bs, WP_ROCKET_LAUNCHER) && forcedFireMode != 2)
				bestWeapon = WP_ROCKET_LAUNCHER;
			else if (BotWeaponSelectableAltFire(bs, WP_REPEATER)) {
				bestWeapon = WP_REPEATER;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 2)
				bestWeapon = WP_CONCUSSION;
			else if (BotWeaponSelectableAltFire(bs, WP_STUN_BATON) && (g_tweakWeapons.integer & WT_STUN_SHOCKLANCE)) {
				bestWeapon = WP_STUN_BATON;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_BLASTER)) {
				bestWeapon = WP_BLASTER;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 1) {
				bestWeapon = WP_CONCUSSION;
				bs->doAltAttack = 1;
			}
			else if (bs->cur_ps.stats[STAT_WEAPONS] & (1 << WP_SABER))
				bestWeapon = WP_SABER;
			else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 1) {
				bestWeapon = WP_DEMP2;
				bs->doAltAttack = 1;
				bs->altChargeTime = 2100;
			}
		}
	}

	else if (hisWeapon == WP_ROCKET_LAUNCHER || hisWeapon == WP_REPEATER || hisWeapon == WP_CONCUSSION || hisWeapon == WP_FLECHETTE) { //Likely going to splash damage us, so dont bother trying to block with saber
		if (distance > 1024) {
			if (BotWeaponSelectable(bs, WP_DISRUPTOR))
				bestWeapon = WP_DISRUPTOR;
			else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 1) {
				bestWeapon = WP_DEMP2;
				bs->doAltAttack = 1;
				bs->altChargeTime = 2100;
			}
			else if (BotWeaponSelectable(bs, WP_BLASTER))
				bestWeapon = WP_BLASTER;
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_OLD)) {
				bestWeapon = WP_BRYAR_OLD;
				bs->doAltAttack = 1;
				bs->altChargeTime = 800;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_PISTOL)) {
				bestWeapon = WP_BRYAR_PISTOL;
				bs->doAltAttack = 1;
				bs->altChargeTime = 1200;
			}
			else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 1) {
				bestWeapon = WP_CONCUSSION;
				bs->doAltAttack = 1;
			}
			else if (bs->cur_ps.stats[STAT_WEAPONS] & (1 << WP_SABER))
				bestWeapon = WP_SABER;
		}
		else if (distance > 350 && distance < 900) { //Have some padding between distance tiers so we dont weaponswitch spam
			if (BotWeaponSelectableAltFire(bs, WP_BLASTER)) {
				bestWeapon = WP_BLASTER;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectable(bs, WP_REPEATER) && forcedFireMode != 2)
				bestWeapon = WP_REPEATER;
			else if (BotWeaponSelectable(bs, WP_DISRUPTOR))
				bestWeapon = WP_DISRUPTOR;
			else if (distance < 768 && BotWeaponSelectableAltFire(bs, WP_STUN_BATON) && (g_tweakWeapons.integer & WT_STUN_LG) && !(g_tweakWeapons.integer & WT_STUN_HEAL)) {
				bestWeapon = WP_STUN_BATON;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_BOWCASTER)) {
				bestWeapon = WP_BOWCASTER;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_OLD)) {
				bestWeapon = WP_BRYAR_OLD;
				bs->doAltAttack = 1;
				bs->altChargeTime = 800;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_PISTOL)) {
				bestWeapon = WP_BRYAR_PISTOL;
				bs->doAltAttack = 1;
				bs->altChargeTime = 1200;
			}
			else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 1) {
				bestWeapon = WP_CONCUSSION;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 1) {
				bestWeapon = WP_DEMP2;
				bs->doAltAttack = 1;
				bs->altChargeTime = 2100;
			}
			else if (bs->cur_ps.stats[STAT_WEAPONS] & (1 << WP_SABER))
				bestWeapon = WP_SABER;
		}
		else if (distance < 200) { //Most DPS!
			if (BotWeaponSelectable(bs, WP_REPEATER) && forcedFireMode != 2)
				bestWeapon = WP_REPEATER;
			else if (BotWeaponSelectableAltFire(bs, WP_BLASTER)) {
				bestWeapon = WP_BLASTER;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectable(bs, WP_FLECHETTE))
				bestWeapon = WP_FLECHETTE;
			else if (BotWeaponSelectable(bs, WP_ROCKET_LAUNCHER))
				bestWeapon = WP_ROCKET_LAUNCHER;
			else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 2)
				bestWeapon = WP_CONCUSSION;
			else if (BotWeaponSelectable(bs, WP_DISRUPTOR))
				bestWeapon = WP_DISRUPTOR;
			else if (BotWeaponSelectableAltFire(bs, WP_BOWCASTER)) {
				bestWeapon = WP_BOWCASTER;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_STUN_BATON) && (g_tweakWeapons.integer & WT_STUN_SHOCKLANCE)) {
				bestWeapon = WP_STUN_BATON;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_OLD)) {
				bestWeapon = WP_BRYAR_OLD;
				bs->doAltAttack = 1;
				bs->altChargeTime = 800;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_PISTOL)) {
				bestWeapon = WP_BRYAR_PISTOL;
				bs->doAltAttack = 1;
				bs->altChargeTime = 1200;
			}
			else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 1) {
				bestWeapon = WP_CONCUSSION;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 1) {
				bestWeapon = WP_DEMP2;
				bs->doAltAttack = 1;
				bs->altChargeTime = 2100;
			}
			else if (bs->cur_ps.stats[STAT_WEAPONS] & (1 << WP_SABER))
				bestWeapon = WP_SABER;
		}
	}


	else { //We can block most of his bullets with saber i guess
		if (distance > 1024) {
			if (BotWeaponSelectable(bs, WP_DISRUPTOR))
				bestWeapon = WP_DISRUPTOR;
			else if (BotWeaponSelectable(bs, WP_BLASTER))
				bestWeapon = WP_BLASTER;
			else if (bs->cur_ps.stats[STAT_WEAPONS] & WP_SABER)
				bestWeapon = WP_SABER;
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_OLD)) {
				bestWeapon = WP_BRYAR_OLD;
				bs->doAltAttack = 1;
				bs->altChargeTime = 800;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_PISTOL)) {
				bestWeapon = WP_BRYAR_PISTOL;
				bs->doAltAttack = 1;
				bs->altChargeTime = 1200;
			}
			else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 1) {
				bestWeapon = WP_CONCUSSION;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 1) {
				bestWeapon = WP_DEMP2;
				bs->doAltAttack = 1;
				bs->altChargeTime = 2100;
			}
		}
		else if (distance > 350 && distance < 900) { //Have some padding between distance tiers so we dont weaponswitch spam
			if (BotWeaponSelectableAltFire(bs, WP_BLASTER)) {
				bestWeapon = WP_BLASTER;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectable(bs, WP_REPEATER) && forcedFireMode != 2)
				bestWeapon = WP_REPEATER;
			else if (BotWeaponSelectable(bs, WP_DISRUPTOR))
				bestWeapon = WP_DISRUPTOR;
			else if (BotWeaponSelectableAltFire(bs, WP_STUN_BATON) && (g_tweakWeapons.integer & WT_STUN_LG) && !(g_tweakWeapons.integer & WT_STUN_HEAL)) {
				bestWeapon = WP_STUN_BATON;
			}
			else if (bs->cur_ps.stats[STAT_WEAPONS] & WP_SABER)
				bestWeapon = WP_SABER;
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_OLD)) {
				bestWeapon = WP_BRYAR_OLD;
				bs->doAltAttack = 1;
				bs->altChargeTime = 800;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_PISTOL)) {
				bestWeapon = WP_BRYAR_PISTOL;
				bs->doAltAttack = 1;
				bs->altChargeTime = 1200;
			}
			else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 1) {
				bestWeapon = WP_CONCUSSION;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 1) {
				bestWeapon = WP_DEMP2;
				bs->doAltAttack = 1;
				bs->altChargeTime = 2100;
			}
		}
		else if (distance < 200) {
			if (BotWeaponSelectable(bs, WP_REPEATER) && forcedFireMode != 2)
				bestWeapon = WP_REPEATER;
			else if (BotWeaponSelectableAltFire(bs, WP_BLASTER)) {
				bestWeapon = WP_BLASTER;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectable(bs, WP_FLECHETTE))
				bestWeapon = WP_FLECHETTE;
			else if (BotWeaponSelectable(bs, WP_ROCKET_LAUNCHER) && forcedFireMode != 2)
				bestWeapon = WP_ROCKET_LAUNCHER;
			else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 2)
				bestWeapon = WP_CONCUSSION;
			else if (BotWeaponSelectable(bs, WP_DISRUPTOR))
				bestWeapon = WP_DISRUPTOR;
			else if (BotWeaponSelectableAltFire(bs, WP_STUN_BATON) && (g_tweakWeapons.integer & WT_STUN_LG) && !(g_tweakWeapons.integer & WT_STUN_HEAL)) {
				bestWeapon = WP_STUN_BATON;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_STUN_BATON) && (g_tweakWeapons.integer & WT_STUN_SHOCKLANCE)) {
				bestWeapon = WP_STUN_BATON;
				bs->doAltAttack = 1;
			}
			else if (bs->cur_ps.stats[STAT_WEAPONS] & WP_SABER)
				bestWeapon = WP_SABER;
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_OLD)) {
				bestWeapon = WP_BRYAR_OLD;
				bs->doAltAttack = 1;
				bs->altChargeTime = 800;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_PISTOL)) {
				bestWeapon = WP_BRYAR_PISTOL;
				bs->doAltAttack = 1;
				bs->altChargeTime = 1200;
			}
			else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 1) {
				bestWeapon = WP_CONCUSSION;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 1) {
				bestWeapon = WP_DEMP2;
				bs->doAltAttack = 1;
				bs->altChargeTime = 2100;
			}
		}
	}

	if (bestWeapon == WP_THERMAL) {
		//bs->doAltAttack = 1;
		bs->altChargeTime = 1250;
		bs->ChargeTime = 1250;
	}
	else if (bestWeapon == WP_BOWCASTER) {
		bs->doAltAttack = 1;
	}

	if (bs->currentEnemy->client && bs->currentEnemy->client->ps.weapon == WP_DEMP2) //dont charge if they can cancel it
		bs->altChargeTime = 50;

	if (forcedFireMode == 1)
		bs->doAltAttack = 0;
	else if (forcedFireMode == 2)
		bs->doAltAttack = 1;

	//todo- weapon table.

	return bestWeapon;
}

int NewBotAI_GetAltCharge(bot_state_t* bs)
{
	int weap;

	weap = bs->cur_ps.weapon;

	if (bs->cur_ps.ammo[weaponData[weap].ammoIndex] < weaponData[weap].altEnergyPerShot && weap != WP_STUN_BATON)
		return 0;

	if ((bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT) && (level.time - bs->cur_ps.weaponChargeTime) > bs->altChargeTime)
		return 2;//release charge.. was 2 ?
	if (bs->cur_ps.weaponstate != WEAPON_CHARGING_ALT)
		return 3;

	return 1;
}

int NewBotAI_GetCharge(bot_state_t* bs)
{
	int weap;

	weap = bs->cur_ps.weapon;

	if (bs->cur_ps.ammo[weaponData[weap].ammoIndex] < weaponData[weap].energyPerShot && weap != WP_STUN_BATON)
		return 0;

	if ((bs->cur_ps.weaponstate == WEAPON_CHARGING) && (level.time - bs->cur_ps.weaponChargeTime) > bs->ChargeTime)
		return 2;//release charge.. was 2 ?
	if (bs->cur_ps.weaponstate != WEAPON_CHARGING)
		return 3;

	return 1;
}

// Check if enemy is within attack cone and range // Niksata Edit
qboolean BotCanHitEnemy(bot_state_t* bs)
{
	vec3_t attackDir, enemyDir, enemyPos;
	float angleDiff, distance;
	trace_t tr;
	vec3_t mins = { -8, -8, -8 }, maxs = { 8, 8, 8 };

	if (!bs->currentEnemy || !bs->currentEnemy->client)
		return qfalse;

	// Get predicted enemy position
	BotPredictEnemyPosition(bs, enemyPos);

	// Check distance first
	VectorSubtract(enemyPos, bs->origin, enemyDir);
	distance = VectorLength(enemyDir);

	// Different ranges for different attack types
	float maxRange = SABER_ATTACK_RANGE;
	if (bs->cur_ps.fd.saberAnimLevel == SS_STAFF)
		maxRange *= 1.2f; // Staff has longer reach
	else if (bs->cur_ps.fd.saberAnimLevel == SS_DUAL)
		maxRange *= 1.1f; // Dual has slightly longer reach

	if (distance > maxRange)
		return qfalse;

	// Check if enemy is in swing cone (±60 degrees for saber)
	AngleVectors(bs->viewangles, attackDir, NULL, NULL);
	VectorNormalize(enemyDir);

	angleDiff = DotProduct(attackDir, enemyDir);
	if (angleDiff < 0.5f) // cos(60) = 0.5
		return qfalse;

	// Trace to ensure clear path
	JP_Trace(&tr, bs->origin, mins, maxs, enemyPos, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);
	if (tr.fraction < 0.8f && tr.entityNum != bs->currentEnemy->s.number)
		return qfalse;

	return qtrue;
}

// Check if bot should attack based on enemy state
qboolean BotShouldAttack(bot_state_t* bs)
{
	if (!bs->currentEnemy || !bs->currentEnemy->client)
		return qfalse;

	// Don't attack if enemy is blocking effectively
	if (bs->currentEnemy->client->ps.saberBlocking && bs->currentEnemy->client->ps.saberBlockTime > level.time)
		return qfalse;

	// Don't attack if enemy is swinging (parry opportunity)
	if (bs->currentEnemy->client->ps.saberInFlight && bs->currentEnemy->client->ps.saberCanThrow)
		return qfalse;

	// Check if we have clear shot
	return BotCanHitEnemy(bs);
}

// Predict enemy position based on current velocity and patterns
void BotPredictEnemyPosition(bot_state_t* bs, vec3_t predictedPos)
{
	vec3_t enemyVel;
	float predictionTime;
	int patternType;

	if (!bs->currentEnemy || !bs->currentEnemy->client) {
		VectorCopy(bs->currentEnemy->client->ps.origin, predictedPos);
		return;
	}

	// Get skill-based prediction time
	predictionTime = BotGetPredictionTime(bs);

	// Get current enemy velocity
	VectorCopy(bs->currentEnemy->client->ps.velocity, enemyVel);

	// Analyze enemy movement pattern
	patternType = BotAnalyzeMovementPattern(bs);

	// Apply pattern-based prediction
	switch (patternType) {
	case 1: // Strafing pattern
		BotPredictStrafingMovement(bs, enemyVel, predictionTime, predictedPos);
		break;
	case 2: // Jumping pattern
		BotPredictJumpingMovement(bs, enemyVel, predictionTime, predictedPos);
		break;
	case 3: // Retreating pattern
		BotPredictRetreatingMovement(bs, enemyVel, predictionTime, predictedPos);
		break;
	case 4: // Charging pattern
		BotPredictChargingMovement(bs, enemyVel, predictionTime, predictedPos);
		break;
	default: // Linear movement
		VectorMA(bs->currentEnemy->client->ps.origin, predictionTime, enemyVel, predictedPos);
		break;
	}

	// Add some randomness based on skill level
	float randomFactor = 1.0f - (bs->settings.skill * 0.05f);
	if (randomFactor > 0.2f) {
		predictedPos[0] += Q_irand(-10, 10) * randomFactor;
		predictedPos[1] += Q_irand(-10, 10) * randomFactor;
	}
}

// Get prediction time based on bot skill
float BotGetPredictionTime(bot_state_t* bs)
{
	//float baseTime = 0.2f; // 200ms base prediction
	float baseTime = 0.15f; // Reduced from 0.2f for less aggressive prediction // mouse wiggle tuning

	// Skill-based adjustment (higher skill = better prediction)
	float skillBonus = bs->settings.skill * 0.03f;

	// Distance-based adjustment
	float distanceBonus = 0.0f;
	if (bs->frame_Enemy_Len > 200) {
		distanceBonus = (bs->frame_Enemy_Len - 200) * 0.0001f;
	}

	return baseTime + skillBonus + distanceBonus;
}

// Analyze enemy movement pattern
int BotAnalyzeMovementPattern(bot_state_t* bs)
{
	vec3_t currentVel, lastVel;
	float velChange;

	if (!bs->currentEnemy || !bs->currentEnemy->client)
		return 0;

	VectorCopy(bs->currentEnemy->client->ps.velocity, currentVel);

	// Check if we have stored velocity history
	if (bs->enemyMovementHistoryTime < level.time - 100) {
		VectorCopy(currentVel, bs->lastEnemyVelocity);
		bs->enemyMovementHistoryTime = level.time;
		return 0;
	}

	VectorCopy(bs->lastEnemyVelocity, lastVel);
	VectorSubtract(currentVel, lastVel, lastVel);
	velChange = VectorLength(lastVel);

	// Update velocity history
	VectorCopy(currentVel, bs->lastEnemyVelocity);
	bs->enemyMovementHistoryTime = level.time;

	// Detect patterns based on velocity changes
	if (velChange > 200) {
		// Sudden velocity change - likely jumping or special move
		if (currentVel[2] > 100) {
			return 2; // Jumping
		}
		else {
			return 3; // Retreating/evading
		}
	}
	else if (velChange > 50) {
		// Moderate change - likely strafing
		return 1; // Strafing
	}
	else if (VectorLength(currentVel) > 150) {
		// High consistent velocity - charging
		return 4; // Charging
	}

	return 0; // Linear movement
}

// Predict strafing movement
void BotPredictStrafingMovement(bot_state_t* bs, vec3_t enemyVel, float predictionTime, vec3_t predictedPos)
{
	vec3_t strafeDir, forward, right;
	float strafeAmount;

	// Calculate strafe direction (perpendicular to forward movement)
	AngleVectors(bs->currentEnemy->client->ps.viewangles, forward, right, NULL);

	// Determine strafe direction based on velocity
	float rightDot = DotProduct(enemyVel, right);
	if (fabs(rightDot) > fabs(DotProduct(enemyVel, forward))) {
		// Primarily strafing
		if (rightDot > 0) {
			VectorCopy(right, strafeDir);
		}
		else {
			VectorScale(right, -1, strafeDir);
		}
		strafeAmount = fabs(rightDot) * predictionTime;
	}
	else {
		// Mixed movement
		VectorScale(forward, DotProduct(enemyVel, forward), strafeDir);
		VectorMA(strafeDir, rightDot, right, strafeDir);
		strafeAmount = VectorLength(strafeDir) * predictionTime;
	}

	// Add strafe prediction with some variation
	VectorMA(bs->currentEnemy->client->ps.origin, strafeAmount * 1.2f, strafeDir, predictedPos);
}

// Predict jumping movement
void BotPredictJumpingMovement(bot_state_t* bs, vec3_t enemyVel, float predictionTime, vec3_t predictedPos)
{
	vec3_t jumpPos;
	float timeToPeak, timeFromPeak;

	// Calculate jump trajectory
	if (enemyVel[2] > 0) {
		// Enemy is rising
		timeToPeak = enemyVel[2] / 800.0f; // Gravity is ~800 units

		if (predictionTime <= timeToPeak) {
			// Still rising
			VectorMA(bs->currentEnemy->client->ps.origin, predictionTime, enemyVel, jumpPos);
			jumpPos[2] += 0.5f * 800 * predictionTime * predictionTime;
		}
		else {
			// Reached peak and falling
			timeFromPeak = predictionTime - timeToPeak;
			float peakHeight = bs->currentEnemy->client->ps.origin[2] +
				enemyVel[2] * timeToPeak - 0.5f * 800 * timeToPeak * timeToPeak;

			VectorMA(bs->currentEnemy->client->ps.origin, timeToPeak, enemyVel, jumpPos);
			jumpPos[2] = peakHeight - 0.5f * 800 * timeFromPeak * timeFromPeak;
		}
	}
	else {
		// Enemy is falling
		VectorMA(bs->currentEnemy->client->ps.origin, predictionTime, enemyVel, jumpPos);
		jumpPos[2] -= 0.5f * 800 * predictionTime * predictionTime;
	}

	VectorCopy(jumpPos, predictedPos);
}

// Predict retreating movement
void BotPredictRetreatingMovement(bot_state_t* bs, vec3_t enemyVel, float predictionTime, vec3_t predictedPos)
{
	vec3_t retreatDir, toBot;
	float retreatSpeed;

	// Calculate retreat direction (away from bot)
	VectorSubtract(bs->origin, bs->currentEnemy->client->ps.origin, toBot);
	VectorNormalize(toBot);

	// Check if enemy is actually moving away
	float retreatDot = DotProduct(enemyVel, toBot);
	if (retreatDot > 0) {
		// Enemy is retreating
		retreatSpeed = VectorLength(enemyVel) * 1.1f; // Slightly accelerate retreat prediction
		VectorMA(bs->currentEnemy->client->ps.origin, predictionTime * retreatSpeed, toBot, predictedPos);
	}
	else {
		// Not clearly retreating, use standard prediction
		VectorMA(bs->currentEnemy->client->ps.origin, predictionTime, enemyVel, predictedPos);
	}
}

// Predict charging movement
void BotPredictChargingMovement(bot_state_t* bs, vec3_t enemyVel, float predictionTime, vec3_t predictedPos)
{
	vec3_t chargeDir, toBot;
	float chargeSpeed;

	// Calculate charge direction (toward bot)
	VectorSubtract(bs->origin, bs->currentEnemy->client->ps.origin, toBot);
	VectorNormalize(toBot);

	// Check if enemy is moving toward bot
	float chargeDot = DotProduct(enemyVel, toBot);
	if (chargeDot < -50) {
		// Enemy is charging
		chargeSpeed = VectorLength(enemyVel) * 1.15f; // Slightly accelerate charge prediction
		VectorScale(toBot, -chargeSpeed, chargeDir);
		VectorMA(bs->currentEnemy->client->ps.origin, predictionTime, chargeDir, predictedPos);
	}
	else {
		// Not clearly charging, use standard prediction
		VectorMA(bs->currentEnemy->client->ps.origin, predictionTime, enemyVel, predictedPos);
	}
}

// Check if bot can attack based on timing and state
qboolean BotCanAttackNow(bot_state_t* bs)
{
	// Check attack cooldown
	if (bs->nextAttackTime > level.time)
		return qfalse;

	// Check if bot is in proper attack state
	if (bs->cur_ps.weaponstate != WEAPON_READY &&
		bs->cur_ps.weaponstate != WEAPON_IDLE)
		return qfalse;

	// Check if bot is not in recovery animation
	if (bs->attackRecoveryTime > level.time)
		return qfalse;

	// Check if bot is not being blocked
	if (bs->cur_ps.saberBlocking && bs->cur_ps.saberBlockTime > level.time)
		return qfalse;

	// Check if enemy is in valid position for attack
	if (!BotCanHitEnemy(bs))
		return qfalse;

	return qtrue;
}

// Execute attack with proper timing
void BotExecuteAttack(bot_state_t* bs)
{
	if (!BotCanAttackNow(bs))
		return;

	// Set attack timing based on saber style
	float attackDelay = BotGetAttackDelay(bs);

	// Execute attack
	trap->EA_Attack(bs->client);

	// Set timing variables
	bs->nextAttackTime = level.time + attackDelay;
	bs->attackRecoveryTime = level.time + (attackDelay * 0.8f);
	bs->lastAttackTime = level.time;

	// Track attack type for combo potential
	bs->lastAttackType = BotGetCurrentAttackType(bs);
	bs->comboWindow = level.time + (attackDelay * 0.3f);
}

// Get attack delay based on saber style and skill
float BotGetAttackDelay(bot_state_t* bs)
{
	float baseDelay = 600.0f; // Base 600ms delay

	// Adjust based on saber style
	switch (bs->cur_ps.fd.saberAnimLevel) {
	case SS_FAST:
		baseDelay = 400.0f; // Fast attacks
		break;
	case SS_MEDIUM:
		baseDelay = 500.0f; // Medium speed
		break;
	case SS_STRONG:
		baseDelay = 800.0f; // Slow but powerful
		break;
	case SS_DESANN:
		baseDelay = 450.0f; // Desann style
		break;
	case SS_TAVION:
		baseDelay = 550.0f; // Tavion style
		break;
	case SS_DUAL:
		baseDelay = 480.0f; // Dual sabers
		break;
	case SS_STAFF:
		baseDelay = 520.0f; // Staff style
		break;
	}

	// Skill-based adjustment (higher skill = faster attacks)
	float skillBonus = (bs->settings.skill * 20.0f);
	baseDelay -= skillBonus;

	// Ensure minimum delay
	if (baseDelay < 300.0f)
		baseDelay = 300.0f;

	return baseDelay;
}

// Get current attack type for combo tracking
int BotGetCurrentAttackType(bot_state_t* bs)
{
	if (!bs->currentEnemy)
		return 0;

	vec3_t toEnemy;
	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->origin, toEnemy);
	float distance = VectorLength(toEnemy);

	if (distance < 64)
		return 1; // Close range attack
	else if (distance < 128)
		return 2; // Medium range attack
	else
		return 3; // Long range attack
}

// Dynamic combat movement - allows positioning during attacks
void BotDynamicCombatMovement(bot_state_t* bs)
{
	if (!bs->currentEnemy || !bs->frame_Enemy_Vis)
		return;

	vec3_t toEnemy, idealPos, moveDir;
	float distance, optimalDistance;
	qboolean shouldMove = qfalse;

	// Calculate current distance to enemy
	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->origin, toEnemy);
	distance = VectorLength(toEnemy);
	VectorNormalize(toEnemy);

	// Determine optimal distance based on saber style
	optimalDistance = BotGetOptimalCombatDistance(bs);

	// Calculate ideal position
	if (distance > optimalDistance + 32) {
		// Too far - move closer
		VectorScale(toEnemy, 1.0f, moveDir);
		shouldMove = qtrue;
	}
	else if (distance < optimalDistance - 32) {
		// Too close - back away
		VectorScale(toEnemy, -1.0f, moveDir);
		shouldMove = qtrue;
	}
	else {
		// At good distance - consider strafing
		if (BotShouldStrafeInCombat(bs)) {
			BotCombatStrafe(bs, moveDir);
			shouldMove = qtrue;
		}
	}

	// Execute movement if needed
	if (shouldMove && !bs->isAttacking) {
		// Allow movement during attack windup but not during swing
		if (bs->attackRecoveryTime < level.time) {
			trap->EA_Move(bs->client, moveDir, 3000.0f);
		}
	}
}

// Get optimal combat distance based on saber style
float BotGetOptimalCombatDistance(bot_state_t* bs)
{
	//float baseDistance = 80.0f;
	float baseDistance = 96.0f; // Increase for more spacing // mouse wiggle tuning

	switch (bs->cur_ps.fd.saberAnimLevel) {
	case SS_FAST:
		baseDistance = 64.0f; // Close range for fast style
		break;
	case SS_MEDIUM:
		baseDistance = 80.0f; // Medium range
		break;
	case SS_STRONG:
		baseDistance = 96.0f; // Longer range for strong style
		break;
	case SS_DUAL:
		baseDistance = 72.0f; // Slightly closer for dual
		break;
	case SS_STAFF:
		baseDistance = 88.0f; // Medium-long for staff
		break;
	default:
		baseDistance = 80.0f;
		break;
	}

	return baseDistance;
}

// Determine if bot should strafe in combat
qboolean BotShouldStrafeInCombat(bot_state_t* bs)
{
	if (!bs->currentEnemy || !bs->frame_Enemy_Vis)
		return qfalse;

	// Don't strafe if actively attacking
	if (bs->doAttack || bs->doAltAttack)
		return qfalse;

	// Don't strafe if recovering from attack
	if (bs->attackRecoveryTime > level.time)
		return qfalse;

	// Skill-based strafing
	//float strafeChance = 0.2f + (bs->settings.skill * 0.1f);
	float strafeChance = 0.1f + (bs->settings.skill * 0.05f); // Reduced from 0.2f // mouse wiggle tuning
	if (Q_irand(0, 100) < (strafeChance * 100))
		return qtrue;

	return qfalse;
}

// Combat strafing with tactical purpose
void BotCombatStrafe(bot_state_t* bs, vec3_t moveDir)
{
	vec3_t right, forward, toEnemy;
	float strafeDuration;

	// Get direction vectors
	AngleVectors(bs->viewangles, forward, right, NULL);
	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->origin, toEnemy);
	VectorNormalize(toEnemy);

	// Determine strafe direction based on tactical situation
	int strafeDirection = BotSelectStrafeDirection(bs);

	switch (strafeDirection) {
	case 1: // Circle right
		VectorCopy(right, moveDir);
		strafeDuration = 400.0f;
		break;
	case 2: // Circle left
		VectorScale(right, -1.0f, moveDir);
		strafeDuration = 400.0f;
		break;
	case 3: // Evade right
		VectorAdd(right, forward, moveDir);
		VectorNormalize(moveDir);
		strafeDuration = 300.0f;
		break;
	case 4: // Evade left
		VectorSubtract(forward, right, moveDir);
		VectorNormalize(moveDir);
		strafeDuration = 300.0f;
		break;
	default:
		return; // No strafing
	}

	// Set strafe timing
	bs->combatStrafeTime = level.time + strafeDuration;
	bs->combatStrafeDir = strafeDirection;

	// Execute strafe
	trap->EA_Move(bs->client, moveDir, 2500.0f);
}

// Select tactical strafe direction
int BotSelectStrafeDirection(bot_state_t* bs)
{
	if (!bs->currentEnemy)
		return 0;

	vec3_t toEnemy, right;
	float rightDot;

	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->origin, toEnemy);
	VectorNormalize(toEnemy);
	AngleVectors(bs->viewangles, NULL, right, NULL);

	rightDot = DotProduct(toEnemy, right);

	// Check enemy attack state
	qboolean enemyAttacking = (bs->currentEnemy->client->ps.weaponstate == WEAPON_FIRING);

	if (enemyAttacking) {
		// Enemy is attacking - prioritize evasion
		if (rightDot > 0) {
			return 3; // Evade right (forward + right)
		}
		else {
			return 4; // Evade left (forward - right)
		}
	}
	else {
		// Enemy not attacking - circle for advantage
		if (Q_irand(0, 1)) {
			return 1; // Circle right
		}
		else {
			return 2; // Circle left
		}
	}
}

// Attack decision making
qboolean BotShouldAttackNow(bot_state_t* bs)
{
	if (!bs->currentEnemy || !bs->frame_Enemy_Vis)
		return qfalse;

	// Distance check
	if (bs->frame_Enemy_Len > SABER_ATTACK_RANGE * 1.5f)
		return qfalse;

	// Timing check
	if (!BotCanAttackNow(bs))
		return qfalse;

	// Opportunity check
	if (!BotShouldAttack(bs))
		return qfalse;

	// Skill-based decision making
	float attackChance = 0.5f + (bs->settings.skill * 0.1f);
	if (Q_irand(0, 100) < (attackChance * 100))
		return qtrue;

	return qfalse;
}

// Update attack timing each frame
void BotUpdateAttackTiming(bot_state_t* bs)
{
	// Update combo window
	if (bs->comboWindow > level.time && bs->comboWindow < level.time + 100) {
		// Combo window closing, try to execute if conditions are right
		if (bs->lastAttackHitTime > bs->lastAttackTime) {
			trap->EA_Attack(bs->client);
		}
	}

	// Check if we should prepare for next attack
	if (bs->nextAttackTime < level.time + 200 && bs->currentEnemy && bs->frame_Enemy_Vis) {
		// Start aiming for next attack
		vec3_t predictedPos;
		BotPredictEnemyPosition(bs, predictedPos);
		VectorSubtract(predictedPos, bs->eye, predictedPos);
		vectoangles(predictedPos, bs->goalAngles);
	}
}

// Update dynamic movement each frame
void BotUpdateDynamicMovement(bot_state_t* bs)
{
	if (!bs->currentEnemy || !bs->frame_Enemy_Vis)
		return;

	// Update combat strafing
	if (bs->combatStrafeTime > level.time) {
		vec3_t strafeDir;
		BotCombatStrafe(bs, strafeDir);
	}

	// Dynamic positioning during combat
	if (bs->nextAttackTime < level.time + 500) {
		BotDynamicCombatMovement(bs);
	}
}

// Movement during attack execution
void BotMovementDuringAttack(bot_state_t* bs)
{
	if (!bs->currentEnemy || !bs->frame_Enemy_Vis)
		return;

	// Only allow movement during attack windup, not during swing
	if (bs->isAttacking && bs->attackRecoveryTime > level.time)
		return;

	vec3_t toEnemy, moveDir;
	float distance;

	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->origin, toEnemy);
	distance = VectorLength(toEnemy);

	// Maintain optimal distance during attack preparation
	float optimalDistance = BotGetOptimalCombatDistance(bs);

	if (distance > optimalDistance + 16) {
		VectorNormalize(toEnemy);
		trap->EA_MoveForward(bs->client);
	}
	else if (distance < optimalDistance - 16) {
		trap->EA_MoveBack(bs->client);
	}
} // Niksata Edit

// ================================ // Niksata Edit
// ENHANCED ENEMY DETECTION
// ================================
qboolean BotEnemyInRecovery(gentity_t* enemy)
{
	if (!enemy || !enemy->client)
		return qfalse;

	// Enhanced recovery detection - after a swing, torsoTimer counts down recovery frames
	if (enemy->client->ps.saberMove != LS_READY &&
		enemy->client->ps.torsoTimer > 0 &&
		enemy->client->ps.torsoTimer < 300)
	{
		return qtrue;
	}

	// Also check for special move recovery
	if (enemy->client->ps.saberMove == LS_A_LUNGE ||
		enemy->client->ps.saberMove == LS_A_JUMP_T__B_ ||
		enemy->client->ps.saberMove == LS_A_FLIP_STAB ||
		enemy->client->ps.saberMove == LS_A_FLIP_SLASH)
	{
		if (enemy->client->ps.torsoTimer > 0 && enemy->client->ps.torsoTimer < 400)
			return qtrue;
	}

	return qfalse;
} // Niksata Edit

qboolean BotCanFeint(bot_state_t* bs) // Niksata Edit
{
	// Can feint if using a saber, not currently in a swing, and has a close enemy
	if (bs->cur_ps.weapon != WP_SABER)
		return qfalse;

	if (bs->frame_Enemy_Len > 140)
		return qfalse;

	if (g_entities[bs->client].client->ps.saberMove != LS_NONE &&
		g_entities[bs->client].client->ps.saberMove != LS_READY)
		return qfalse;

	// Enhanced feint conditions
	// Feint more against skilled opponents
	if (bs->currentEnemy->client->ps.saberMove != LS_NONE) {
		if (Q_irand(1, 10) < 4) return qtrue;
	}

	// Feint when enemy has health advantage
	int myHealth = g_entities[bs->client].health;
	int enemyHealth = bs->currentEnemy->health;
	if (enemyHealth > myHealth * 1.2f) {
		if (Q_irand(1, 10) < 6) return qtrue;
	}

	// Random feint for unpredictability
	return (Q_irand(1, 20) == 1);
} // Niksata Edit

// Enhanced enemy swing detection // Niksata Edit
qboolean BotEnemySwinging(gentity_t* enemy) {
	if (!enemy || !enemy->client) return qfalse;

	int move = enemy->client->ps.saberMove;

	// More comprehensive swing detection
	if (move >= LS_A_TL2BR && move <= LS_S_BL2TR) {
		return qtrue;
	}

	// Check for special moves
	if (move == LS_A_LUNGE || move == LS_A_JUMP_T__B_ ||
		move == LS_A_FLIP_STAB || move == LS_A_FLIP_SLASH) {
		return qtrue;
	}

	return qfalse;
} // Niksata Edit

qboolean BotEnemyVulnerable(gentity_t* enemy) { // Niksata Edit
	if (!enemy || !enemy->client) return qfalse;

	// Enemy is in recovery
	if (BotEnemyInRecovery(enemy)) return qtrue;

	// Enemy is idle
	if (enemy->client->ps.saberMove == LS_READY) return qtrue;

	// Enemy is jumping (vulnerable)
	if (enemy->client->ps.groundEntityNum == ENTITYNUM_NONE) return qtrue;

	return qfalse;
} // Niksata Edit

// Quads: directions for saber swings relative to bot's view // Niksata Edit
void TAB_MoveforAttackQuad(bot_state_t* bs, vec3_t moveDir, int Quad)
{
	vec3_t forward, right, temp;
	AngleVectors(bs->viewangles, forward, right, NULL);

	switch (Quad)
	{
	case Q_B:   VectorCopy(forward, moveDir); break;               // Down
	case Q_BR:  VectorAdd(forward, right, moveDir); VectorNormalize(moveDir); break; // Down-right
	case Q_R:   VectorCopy(right, moveDir); break;                // Right
	case Q_TR:  VectorScale(forward, -1, forward); VectorAdd(forward, right, moveDir); VectorNormalize(moveDir); break; // Up-right
	case Q_T:   VectorScale(forward, -1, forward); VectorCopy(forward, moveDir); break; // Up
	case Q_TL:  VectorScale(forward, -1, forward); VectorScale(right, -1, temp); VectorAdd(forward, temp, moveDir); VectorNormalize(moveDir); break; // Up-left
	case Q_L:   VectorScale(right, -1, moveDir); break;            // Left
	case Q_BL:  VectorScale(right, -1, right); VectorAdd(forward, right, moveDir); VectorNormalize(moveDir); break; // Down-left
	default:    VectorCopy(forward, moveDir); break;               // Fallback
	}
} // Niksata Edit

// Enhanced feint execution // Niksata Edit
void Bot_ExecuteFeint(bot_state_t* bs) {
	if (!bs->isFeinting) return;

	int fTime = level.time - bs->feintTime;

	if (fTime > 150 && fTime < 260) {
		// Smart retreat during feint
		trap->EA_MoveBack(bs->client);

		// Add random strafe to confuse enemy
		if (Q_irand(1, 10) < 5) {
			if (Q_irand(0, 1)) trap->EA_MoveRight(bs->client);
			else trap->EA_MoveLeft(bs->client);
		}
		return;
	}

	// Counter-attack if enemy commits to swing
	if (BotEnemySwinging(bs->currentEnemy) && fTime < 700) {
		bs->isFeinting = qfalse;

		// Choose counter based on enemy swing direction
		int enemyMove = bs->currentEnemy->client->ps.saberMove;
		vec3_t counterMove;

		if (enemyMove == LS_A_L2R || enemyMove == LS_S_L2R) {
			// Enemy swinging left-right, counter from right
			TAB_MoveforAttackQuad(bs, counterMove, Q_BR);
		}
		else if (enemyMove == LS_A_R2L || enemyMove == LS_S_R2L) {
			// Enemy swinging right-left, counter from left
			TAB_MoveforAttackQuad(bs, counterMove, Q_BL);
		}
		else {
			// Default counter
			TAB_MoveforAttackQuad(bs, counterMove, Q_B);
		}

		trap->EA_Move(bs->client, counterMove, 5000);
		trap->EA_Attack(bs->client);
		return;
	}

	if (fTime > 700) {
		bs->isFeinting = qfalse;
	}
} // Niksata Edit

// ================================ // Niksata Edit
// ENEMY ATTACK ANALYSIS
// ================================
void Bot_AnalyzeEnemyAttack(bot_state_t* bs) {
	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client) return;

	gentity_t* enemy = bs->currentEnemy;
	botBlocking_t* blk = &bs->blocking;

	// Get enemy swing state
	int torsoTimer = enemy->client->ps.torsoTimer;
	int saberMove = enemy->client->ps.saberMove;

	// INITIALIZE enemySpeed with default value
	vec3_t enemyVel;
	float enemySpeed = 0.0f;

	// Determine swing stage (1-8 system from tutorial)
	if (saberMove == LS_READY || saberMove == LS_NONE) {
		blk->enemySwingStage = 0; // Not swinging
		enemySpeed = 0.0f; // Explicitly set for non-swinging
	}
	else if (torsoTimer > 400) {
		blk->enemySwingStage = 1; // Beginning, ghosting phase
	}
	else if (torsoTimer > 300) {
		blk->enemySwingStage = 2; // Parry stage
	}
	else if (torsoTimer > 200) {
		blk->enemySwingStage = 3; // Early damage
	}
	else if (torsoTimer > 100) {
		blk->enemySwingStage = 4; // Maximum damage
	}
	else if (torsoTimer > 50) {
		blk->enemySwingStage = 5; // Slowing damage
	}
	else if (torsoTimer > 25) {
		blk->enemySwingStage = 6; // Near-end damage
	}
	else {
		blk->enemySwingStage = 7; // End of damage
	}

	// Calculate attack power (simplified G_SaberAttackPower)
	blk->enemyAttackPower = 0;

	if (saberMove != LS_READY && saberMove != LS_NONE) {
		// Base power from swing stage
		if (blk->enemySwingStage >= 3 && blk->enemySwingStage <= 5) {
			blk->enemyAttackPower = 7.0f; // Maximum power stages
		}
		else if (blk->enemySwingStage == 2 || blk->enemySwingStage == 6) {
			blk->enemyAttackPower = 5.0f; // Moderate power stages
		}
		else {
			blk->enemyAttackPower = 3.0f; // Low power stages
		}

		// Stance multiplier (Strong > Staff > Duals > Medium > Fast)
		switch (enemy->client->ps.fd.saberAnimLevel) {
		case SS_STRONG:
			blk->enemyAttackPower *= 1.5f;
			break;
		case SS_DESANN:  // Fixed typo
			blk->enemyAttackPower *= 1.5f;
			break;
		case SS_STAFF:
			blk->enemyAttackPower *= 1.3f;
			break;
		case SS_DUAL:
			blk->enemyAttackPower *= 1.2f;
			break;
		case SS_MEDIUM:
			blk->enemyAttackPower *= 1.1f;
			break;
		case SS_TAVION:  // Fixed typo
			blk->enemyAttackPower *= 1.1f;
			break;
		case SS_FAST:
			blk->enemyAttackPower *= 1.0f;
			break;
		}

		// Movement multiplier (Offense: Running > Walking > Standing > Crouching)
		VectorCopy(enemy->client->ps.velocity, enemyVel);
		enemySpeed = VectorLength(enemyVel); // Always set when swinging

		if (enemySpeed > 200) {
			blk->enemyAttackPower *= 1.3f; // Running boost
		}
		else if (enemySpeed > 100) {
			blk->enemyAttackPower *= 1.1f; // Walking boost
		}
		else if (enemy->client->ps.pm_flags & PMF_DUCKED) {
			blk->enemyAttackPower *= 0.8f; // Crouching penalty
		}

		// Attacker boost (from tutorial code)
		blk->enemyAttackPower *= 2.0f;
	}
	else {
		// IMPORTANT: Handle case where enemy is NOT swinging
		VectorCopy(enemy->client->ps.velocity, enemyVel);
		enemySpeed = VectorLength(enemyVel); // Set enemySpeed even when not swinging
	}

	// Enemy movement state - NOW WORKS (enemySpeed always set)
	blk->enemyMoving = (enemySpeed > 50);
	blk->enemyStance = enemy->client->ps.fd.saberAnimLevel;
} // Niksata Edit

// ================================ // Niksata Edit
// PERFECT BLOCKING EXECUTION
// ================================
void Bot_ExecutePerfectBlocking(bot_state_t* bs) {
	if (!bs || !bs->currentEnemy || bs->cur_ps.weapon != WP_SABER) return;

	botBlocking_t* blk = &bs->blocking;
	Bot_AnalyzeEnemyAttack(bs); // This fills all the structure fields

	int dist = bs->frame_Enemy_Len;

	// ================================
	// DEFENSIVE STANCE OPTIMIZATION
	// ================================

	// Optimize defense based on enemy attack stage
	if (blk->enemySwingStage >= 2 && blk->enemySwingStage <= 6) {
		// Enemy in active swing - optimize defense

		/*// Crouch for maximum stability vs strong styles // Niksata Edit
		if (blk->enemyStance == SS_STRONG && dist < 120) {
			trap->EA_Crouch(bs->client);
		}*/ // Niksata Edit

		// Stop moving for maximum block stability
		trap->EA_MoveRight(bs->client, 0);
		trap->EA_MoveLeft(bs->client, 0);
		trap->EA_MoveForward(bs->client, 0);
		trap->EA_MoveBack(bs->client, 0);

		// Calculate optimal block angle
		vec3_t toEnemy, right, forward;
		VectorSubtract(bs->currentEnemy->client->ps.origin, bs->origin, toEnemy);
		VectorNormalize(toEnemy);
		AngleVectors(bs->viewangles, forward, right, NULL);

		// Look down slightly for better block angle (from tutorial)
		bs->ideal_viewangles[PITCH] = -10.0f;

		// Add wiggle for increased block chance (from tutorial)
		float wiggleAmount = 0.0f;
		if (blk->enemyStance == SS_STAFF) {
			wiggleAmount = 15.0f; // More wiggle vs staff
		}
		else if (blk->enemyStance == SS_DUAL) {
			wiggleAmount = 10.0f; // Moderate wiggle vs duals
		}
		else {
			wiggleAmount = 5.0f; // Minimal wiggle vs single
		}

		bs->ideal_viewangles[YAW] += Q_flrand(-wiggleAmount, wiggleAmount);

		// Check for parry opportunity (Stage 2 from tutorial)
		if (blk->enemySwingStage == 2 && (level.time - blk->lastParryTime) > 500) {
			blk->shouldParry = qtrue;
			blk->parryWindow = level.time + 100; // 100ms parry window
		}

		return;
	}

	// ================================
	// PARRY EXECUTION
	// ================================

	if (blk->shouldParry && level.time < blk->parryWindow) {
		Bot_ExecuteParry(bs);
		return;
	}

	// ================================
	// OVERWHELM PREVENTION
	// ================================

	// Check if we're being overwhelmed (from tutorial)
	if (blk->enemyAttackPower > 10.0f && blk->enemyStance == SS_STAFF) {
		// Staff overwhelm - create distance immediately
		trap->EA_MoveBack(bs->client);

		// Jump to escape overwhelm
		if (bs->cur_ps.groundEntityNum != ENTITYNUM_NONE) {
			trap->EA_Jump(bs->client);
		}

		blk->blockConfidence -= 0.1f;
		return;
	}

	// ================================
	// RESET PARRY PREPARATION
	// ================================

	// Prepare for reset parry (attacking during swing reset stage 8)
	if (blk->enemySwingStage == 8 && (level.time - blk->lastBlockTime) < 200) {
		if (Q_irand(1, 10) < 3) { // 30% chance for reset parry
			blk->shouldParry = qtrue;
			blk->parryWindow = level.time + 50; // Very short window
		}
	}
} // Niksata Edit

// ================================ // Niksata Edit
// PARRY EXECUTION
// ================================
void Bot_ExecuteParry(bot_state_t* bs) {
	if (!bs || !bs->currentEnemy) return;

	botBlocking_t* blk = &bs->blocking;
	vec3_t attackDir;

	// Choose counter based on enemy swing direction (from tutorial)
	int enemyMove = bs->currentEnemy->client->ps.saberMove;

	if (enemyMove == LS_A_L2R || enemyMove == LS_S_L2R ||
		enemyMove == LS_A_TL2BR || enemyMove == LS_S_TL2BR) {
		// Enemy swinging left-right, counter from right
		TAB_MoveforAttackQuad(bs, attackDir, Q_BR);
	}
	else if (enemyMove == LS_A_R2L || enemyMove == LS_S_R2L ||
		enemyMove == LS_A_TR2BL || enemyMove == LS_S_TR2BL) {
		// Enemy swinging right-left, counter from left
		TAB_MoveforAttackQuad(bs, attackDir, Q_BL);
	}
	else if (enemyMove == LS_A_T2B || enemyMove == LS_S_T2B) {
		// Enemy swinging top-down, counter from sides
		TAB_MoveforAttackQuad(bs, attackDir, Q_irand(Q_R, Q_L));
	}
	else {
		// Default counter
		TAB_MoveforAttackQuad(bs, attackDir, Q_B);
	}

	// Execute parry with maximum speed (faster than normal attack)
	trap->EA_Move(bs->client, attackDir, 8000); // Higher speed for parry
	trap->EA_Attack(bs->client);

	// Update parry tracking
	blk->lastParryTime = level.time;
	blk->shouldParry = qfalse;
	blk->blockStreak++;
	blk->blockConfidence += 0.2f;

	// Cap confidence
	if (blk->blockConfidence > 1.0f) {
		blk->blockConfidence = 1.0f;
	}
} // Niksata Edit

// ================================ // Niksata Edit
// PERFECT BLOCKING INITIALIZATION
// ================================
void Bot_InitPerfectBlocking(bot_state_t* bs) {
	if (!bs) return;

	memset(&bs->blocking, 0, sizeof(botBlocking_t));
	bs->blocking.blockConfidence = 0.5f; // Start with moderate confidence
	bs->blocking.lastBlockTime = level.time - 1000; // Prevent immediate blocks
	bs->blocking.lastParryTime = level.time - 1000; // Prevent immediate parries
} // Niksata Edit

// ================================  // Niksata Edit
// TACTICAL STYLE ADVANTAGE SYSTEM
// ================================
float Bot_GetStyleAdvantage(int myStyle, int enemyStyle) {
	switch (myStyle) {
	case SS_FAST:
		if (enemyStyle == SS_STRONG || enemyStyle == SS_DESANN) return 0.8f; // Strong advantage
		if (enemyStyle == SS_MEDIUM) return -0.3f; // Disadvantage
		if (enemyStyle == SS_STAFF) return 0.2f; // Slight advantage
		return 0.0f; // Neutral

	case SS_TAVION:
		if (enemyStyle == SS_STRONG || enemyStyle == SS_DESANN) return 0.9f; // Very strong advantage
		if (enemyStyle == SS_FAST) return 0.1f; // Slight advantage
		if (enemyStyle == SS_MEDIUM) return -0.2f; // Slight disadvantage
		return 0.0f; // Neutral

	case SS_MEDIUM:
		if (enemyStyle == SS_FAST || enemyStyle == SS_TAVION) return 0.7f; // Strong advantage
		if (enemyStyle == SS_STRONG) return -0.4f; // DISADVANTAGE
		if (enemyStyle == SS_DESANN) return -0.5f; // Major disadvantage
		if (enemyStyle == SS_DUAL) return 0.3f; // Advantage
		return 0.0f; // Neutral

	case SS_DESANN:
		if (enemyStyle == SS_FAST || enemyStyle == SS_TAVION) return 0.8f; // Strong advantage
		if (enemyStyle == SS_MEDIUM) return 0.5f; // Strong advantage
		if (enemyStyle == SS_STRONG) return 0.2f; // Slight advantage (Desann > Strong)
		if (enemyStyle == SS_STAFF) return 0.6f; // Strong advantage
		if (enemyStyle == SS_DUAL) return 0.5f; // Advantage
		return 0.0f; // Neutral

	case SS_STRONG:
		if (enemyStyle == SS_MEDIUM) return 0.4f; // ADVANTAGE
		if (enemyStyle == SS_FAST) return -0.5f; // Major disadvantage
		if (enemyStyle == SS_TAVION) return -0.6f; // Major disadvantage
		if (enemyStyle == SS_STAFF) return 0.3f; // Advantage
		if (enemyStyle == SS_DUAL) return 0.2f; // Slight advantage
		return 0.0f; // Neutral

	case SS_STAFF:
		if (enemyStyle == SS_FAST) return 0.4f; // Advantage
		if (enemyStyle == SS_MEDIUM) return 0.2f; // Slight advantage
		if (enemyStyle == SS_STRONG) return -0.3f; // Disadvantage
		if (enemyStyle == SS_DESANN) return -0.6f; // Major disadvantage
		return 0.0f; // Neutral

	case SS_DUAL:
		if (enemyStyle == SS_STRONG) return 0.3f; // Advantage
		if (enemyStyle == SS_MEDIUM) return -0.3f; // Disadvantage
		if (enemyStyle == SS_STAFF) return 0.1f; // Slight advantage
		if (enemyStyle == SS_DESANN) return -0.5f; // Major disadvantage
		return 0.0f; // Neutral
	}
	return 0.0f;
} // Niksata Edit

// ================================ // Niksata Edit
// INTEGRATED INTELLIGENT SABER STYLE SELECTION (SIMPLIFIED)
// ================================
void Bot_SelectOptimalSaberStyle(bot_state_t* bs) {
	int dist = bs->frame_Enemy_Len;
	int myHealth = g_entities[bs->client].health;
	int enemyHealth = bs->currentEnemy->health;
	int myForceLevel = g_entities[bs->client].client->ps.fd.forcePowerLevel[FP_SABER_OFFENSE];
	int currentStyle = g_entities[bs->client].client->ps.fd.saberAnimLevel;

	// Check if enemy is human or bot
	qboolean enemyIsHuman = (bs->currentEnemy &&
		!(bs->currentEnemy->r.svFlags & SVF_BOT));

	// ================================ // Niksata Edit
	// Niksata Edit - KATA PROTECTION CHECK
	// ================================
	// Don't switch styles during katas - this interrupts the kata
	if (BG_SaberInKata(bs->cur_ps.saberMove)) {
		return;
	} // Niksata Edit

	// Don't switch if in combat or on cooldown
	if ((g_entities[bs->client].client->ps.saberMove != LS_NONE &&
		g_entities[bs->client].client->ps.saberMove != LS_READY) ||
		level.time < bs->lastStyleSwitchTime + 2000) {
		return;
	}

	// Skip Staff/Dual styles (they have their own logic)
	if (currentStyle == SS_STAFF || currentStyle == SS_DUAL) {
		return;
	}

	// ================================
	// VS HUMAN PLAYERS: COUNTER THEIR STYLE
	// ================================
	if (enemyIsHuman) {
		int enemyStyle = bs->currentEnemy->client->ps.fd.saberAnimLevel;

		// Priority 1: Self-preservation (low health)
		if (myHealth < 40) {
			if (currentStyle != SS_FAST) {
				Cmd_SaberAttackCycle_f(&g_entities[bs->client]);
				bs->lastStyleSwitchTime = level.time;
			}
			return;
		}

		// Priority 2: Counter enemy's style
		switch (enemyStyle) {
		case SS_STRONG:
			// Counter strong with fast (speed beats power)
			if (currentStyle != SS_FAST && myForceLevel > 0) {
				Cmd_SaberAttackCycle_f(&g_entities[bs->client]);
				bs->lastStyleSwitchTime = level.time;
			}
			break;

		case SS_FAST:
			// Counter fast with medium (balanced beats speed)
			if (currentStyle != SS_MEDIUM && myForceLevel > 1) {
				Cmd_SaberAttackCycle_f(&g_entities[bs->client]);
				bs->lastStyleSwitchTime = level.time;
			}
			break;

		case SS_MEDIUM:
			// Counter medium with strong (power beats balanced)
			if (currentStyle != SS_STRONG && myForceLevel > 2 && bs->saberPower) {
				Cmd_SaberAttackCycle_f(&g_entities[bs->client]);
				bs->lastStyleSwitchTime = level.time;
			}
			else if (currentStyle != SS_FAST && myForceLevel > 0) {
				// Fallback to fast if strong not available
				Cmd_SaberAttackCycle_f(&g_entities[bs->client]);
				bs->lastStyleSwitchTime = level.time;
			}
			break;

		case SS_DESANN:
			// Desann style is like strong - counter with fast
			if (currentStyle != SS_FAST && myForceLevel > 0) {
				Cmd_SaberAttackCycle_f(&g_entities[bs->client]);
				bs->lastStyleSwitchTime = level.time;
			}
			break;

		case SS_TAVION:
			// Tavion style is like medium - counter with strong
			if (currentStyle != SS_STRONG && myForceLevel > 2 && bs->saberPower) {
				Cmd_SaberAttackCycle_f(&g_entities[bs->client]);
				bs->lastStyleSwitchTime = level.time;
			}
			else if (currentStyle != SS_FAST && myForceLevel > 0) {
				// Fallback to fast
				Cmd_SaberAttackCycle_f(&g_entities[bs->client]);
				bs->lastStyleSwitchTime = level.time;
			}
			break;

		default:
			// Unknown style - use distance-based logic
			// Continue to distance-based logic below
			break;
		}

		// If we handled the style above, return
		if (enemyStyle >= SS_FAST && enemyStyle <= SS_TAVION) {
			return;
		}
	}

	// ================================
	// VS BOTS: VARIETY AND SITUATIONAL SWITCHING
	// ================================
	else {
		// Priority 1: Self-preservation (low health)
		if (myHealth < 40) {
			if (currentStyle != SS_FAST) {
				Cmd_SaberAttackCycle_f(&g_entities[bs->client]);
				bs->lastStyleSwitchTime = level.time;
			}
			return;
		}

		// Priority 2: Time-based variety switching (every 8-15 seconds)
		if (level.time > bs->lastStyleSwitchTime + Q_irand(8000, 15000)) {
			int availableStyles[5] = { SS_FAST, SS_MEDIUM, SS_STRONG, SS_DESANN, SS_TAVION };
			int availableCount = 0;

			// Check which styles are available based on force level
			if (myForceLevel > 0) availableStyles[availableCount++] = SS_FAST;
			if (myForceLevel > 1) availableStyles[availableCount++] = SS_MEDIUM;
			if (myForceLevel > 2 && bs->saberPower) availableStyles[availableCount++] = SS_STRONG;
			if (myForceLevel > 2 && bs->saberPower) availableStyles[availableCount++] = SS_DESANN;
			if (myForceLevel > 2 && bs->saberPower) availableStyles[availableCount++] = SS_TAVION;

			if (availableCount > 0) {
				int randomStyle = availableStyles[Q_irand(0, availableCount - 1)];
				if (currentStyle != randomStyle) {
					Cmd_SaberAttackCycle_f(&g_entities[bs->client]);
					bs->lastStyleSwitchTime = level.time;
				}
			}
			return;
		}

		// If not switching for variety, continue to distance-based logic
	}

	// ================================
	// DISTANCE-BASED OPTIMIZATION (Common for both)
	// ================================
	if (dist <= 80) {
		// Close range - fast style
		if (currentStyle != SS_FAST && myForceLevel > 0) {
			Cmd_SaberAttackCycle_f(&g_entities[bs->client]);
			bs->lastStyleSwitchTime = level.time;
		}
	}
	else if (dist >= 120) {
		// Long range - strong style if available
		if (enemyHealth > 60 && myForceLevel > 2 && currentStyle != SS_STRONG && bs->saberPower) {
			Cmd_SaberAttackCycle_f(&g_entities[bs->client]);
			bs->lastStyleSwitchTime = level.time;
		}
		else if (myForceLevel > 1 && currentStyle != SS_MEDIUM) {
			Cmd_SaberAttackCycle_f(&g_entities[bs->client]);
			bs->lastStyleSwitchTime = level.time;
		}
	}
	else {
		// Medium range - balanced approach
		if (enemyHealth > 50 && myForceLevel > 1 && currentStyle != SS_MEDIUM) {
			Cmd_SaberAttackCycle_f(&g_entities[bs->client]);
			bs->lastStyleSwitchTime = level.time;
		}
		else if (currentStyle != SS_FAST && myForceLevel > 0) {
			Cmd_SaberAttackCycle_f(&g_entities[bs->client]);
			bs->lastStyleSwitchTime = level.time;
		}
	}
} // Niksata Edit

// ================================
// MULTI-SABER COMBAT FUNCTIONS
// ================================

void Bot_ExecuteSaberDance(bot_state_t* bs) {
	// A-D-A-D-A-D pattern for defensive wall
	int dist = bs->frame_Enemy_Len;

	// Range check - only execute if in saber range
	if (dist > 80) return;

	vec3_t moveDir;
	int dancePattern = Q_irand(0, 3);

	switch (dancePattern) {
	case 0: TAB_MoveforAttackQuad(bs, moveDir, Q_R); break;
	case 1: TAB_MoveforAttackQuad(bs, moveDir, Q_L); break;
	case 2: TAB_MoveforAttackQuad(bs, moveDir, Q_R); break;
	case 3: TAB_MoveforAttackQuad(bs, moveDir, Q_L); break;
	}

	trap->EA_Move(bs->client, moveDir, 3000);
	trap->EA_Attack(bs->client);
	bs->lastDanceTime = level.time;
}

void Bot_ExecuteRushAttack(bot_state_t* bs) {
	// WA-WD pattern for forward pressure
	int dist = bs->frame_Enemy_Len;

	// Range check - only execute if in range
	if (dist > 100) return;

	vec3_t moveDir;
	TAB_MoveforAttackQuad(bs, moveDir, Q_T);
	trap->EA_Move(bs->client, moveDir, 4000);
	trap->EA_Attack(bs->client);
}

void Bot_ExecuteDFA(bot_state_t* bs)
{
	int dist = bs->frame_Enemy_Len;
	vec3_t enemyPos, botPos, moveDir;
	vec3_t angles;

	// Range check - DFA effective range
	if (dist < 50 || dist > 200) return;

	/*// Get enemy position
	if (!bs->currentEnemy) return;
	VectorCopy(bs->currentEnemy->r.currentOrigin, enemyPos);

	// Calculate direction to enemy
	VectorSubtract(enemyPos, botPos, moveDir);
	moveDir[2] = 0; // Keep horizontal
	VectorNormalize(moveDir);
	vectoangles(moveDir, angles);

	// Face enemy
	trap->EA_View(bs->client, angles);*/

	// JKA DFA: jump+forward+attack combo
	trap->EA_MoveForward(bs->client);
	trap->EA_Jump(bs->client);
	trap->EA_Attack(bs->client);

}

void Bot_ExecuteCrouchLunge(bot_state_t* bs)
{
	int dist = bs->frame_Enemy_Len;
	vec3_t enemyPos, botPos, moveDir;
	vec3_t angles;

	// Range check - DFA effective range
	if (dist > 100) return;

	trap->EA_Crouch(bs->client);
	trap->EA_MoveForward(bs->client);
	trap->EA_Attack(bs->client);

}

void Bot_ExecuteHurricane(bot_state_t* bs) {
	// WA-SD or WD-AS diagonal pattern
	int dist = bs->frame_Enemy_Len;

	// Range check - only execute if in range
	if (dist > 90) return;

	vec3_t moveDir;
	int hurricanePattern = Q_irand(0, 1);

	switch (hurricanePattern) {
	case 0: TAB_MoveforAttackQuad(bs, moveDir, Q_TL); break;
	case 1: TAB_MoveforAttackQuad(bs, moveDir, Q_TR); break;
	}

	trap->EA_Move(bs->client, moveDir, 3500);
	trap->EA_Attack(bs->client);
}

void Bot_ExecuteStyleSwitch(bot_state_t* bs) {
	// Switch to fast/medium then back to staff/dual for damage boost
	int dist = bs->frame_Enemy_Len;

	// Range check - only execute if in range
	if (dist > 120) return;

	int currentStyle = g_entities[bs->client].client->ps.fd.saberAnimLevel;
	int myForceLevel = g_entities[bs->client].client->ps.fd.forcePowerLevel[FP_SABER_OFFENSE];

	if ((currentStyle == SS_STAFF || currentStyle == SS_DUAL) && myForceLevel > 1) {
		// Switch to fast for the switch
		Cmd_SaberAttackCycle_f(&g_entities[bs->client]);
		bs->styleSwitchTime = level.time + 200; // Wait for switch

		// Switch back after delay
		if (level.time >= bs->styleSwitchTime) {
			Cmd_SaberAttackCycle_f(&g_entities[bs->client]);
		}
	}
}

void Bot_ExecuteDelayAttack(bot_state_t* bs) {
	// A-Strike Delay - wait for reset then strike
	int dist = bs->frame_Enemy_Len;

	// Range check - only execute if in range
	if (dist > 110) return;

	vec3_t moveDir;

	// First strike (any direction)
	TAB_MoveforAttackQuad(bs, moveDir, Q_irand(Q_R, Q_L));
	trap->EA_Move(bs->client, moveDir, 4000);
	trap->EA_Attack(bs->client);

	// Wait for reset stage
	bs->delayAttackTime = level.time + 300; // Wait for reset

	if (level.time >= bs->delayAttackTime) {
		// Second A-strike for delayed hit
		TAB_MoveforAttackQuad(bs, moveDir, Q_B);
		trap->EA_Move(bs->client, moveDir, 6000); // Faster for delay
		trap->EA_Attack(bs->client);
	}
}

void Bot_ExecuteConvergentAttack(bot_state_t* bs) {
	// Convergent attack - move while striking
	int dist = bs->frame_Enemy_Len;

	// Range check - only execute if in range
	if (dist > 100) return;

	vec3_t moveDir;
	vec3_t toEnemy;
	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->origin, toEnemy);
	VectorNormalize(toEnemy);

	// Start attack
	TAB_MoveforAttackQuad(bs, moveDir, Q_B);
	trap->EA_Move(bs->client, moveDir, 5000);
	trap->EA_Attack(bs->client);

	// Continue movement (convergent)
	if (Q_irand(1, 100) <= 60) {
		vec3_t right, forward;
		AngleVectors(bs->viewangles, forward, right, NULL);

		// Move towards enemy while attacking
		VectorScale(forward, 100.0f, moveDir);
		trap->EA_Move(bs->client, moveDir, 2000);
	}
}

void Bot_ExecuteStaticAttack(bot_state_t* bs) {
	// Static attack - keep saber in one place
	int dist = bs->frame_Enemy_Len;

	// Range check - only execute if in range
	if (dist > 90) return;

	vec3_t moveDir;

	// Aim at enemy but don't move saber
	TAB_MoveforAttackQuad(bs, moveDir, Q_B);
	trap->EA_Move(bs->client, moveDir, 0); // No movement
	trap->EA_Attack(bs->client);

	// Slight adjustment to maintain aim
	if (Q_irand(1, 100) <= 30) {
		VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, moveDir);
		VectorNormalize(moveDir);
		trap->EA_Move(bs->client, moveDir, 500); // Minor adjustment
	}
}

void Bot_ExecuteThreeSwingCombo(bot_state_t* bs) {
	// Three-swing combo system
	int dist = bs->frame_Enemy_Len;

	// Range check - only execute if in range
	if (dist > 120) return;

	vec3_t moveDir;
	int comboPattern = Q_irand(0, 5);

	switch (comboPattern) {
	case 0: // W-W-WD
		TAB_MoveforAttackQuad(bs, moveDir, Q_T); trap->EA_Attack(bs->client);
		TAB_MoveforAttackQuad(bs, moveDir, Q_T); trap->EA_Attack(bs->client);
		TAB_MoveforAttackQuad(bs, moveDir, Q_BR); trap->EA_Attack(bs->client);
		break;
	case 1: // W-WD-D
		TAB_MoveforAttackQuad(bs, moveDir, Q_T); trap->EA_Attack(bs->client);
		TAB_MoveforAttackQuad(bs, moveDir, Q_BR); trap->EA_Attack(bs->client);
		TAB_MoveforAttackQuad(bs, moveDir, Q_R); trap->EA_Attack(bs->client);
		break;
	case 2: // WD-W-WD
		TAB_MoveforAttackQuad(bs, moveDir, Q_BR); trap->EA_Attack(bs->client);
		TAB_MoveforAttackQuad(bs, moveDir, Q_T); trap->EA_Attack(bs->client);
		TAB_MoveforAttackQuad(bs, moveDir, Q_BR); trap->EA_Attack(bs->client);
		break;
	case 3: // A-A-AS
		TAB_MoveforAttackQuad(bs, moveDir, Q_L); trap->EA_Attack(bs->client);
		TAB_MoveforAttackQuad(bs, moveDir, Q_L); trap->EA_Attack(bs->client);
		TAB_MoveforAttackQuad(bs, moveDir, Q_BL); trap->EA_Attack(bs->client);
		break;
	case 4: // A-A-AW
		TAB_MoveforAttackQuad(bs, moveDir, Q_L); trap->EA_Attack(bs->client);
		TAB_MoveforAttackQuad(bs, moveDir, Q_L); trap->EA_Attack(bs->client);
		TAB_MoveforAttackQuad(bs, moveDir, Q_TL); trap->EA_Attack(bs->client);
		break;
	case 5: // AW-A-AW
		TAB_MoveforAttackQuad(bs, moveDir, Q_TL); trap->EA_Attack(bs->client);
		TAB_MoveforAttackQuad(bs, moveDir, Q_L); trap->EA_Attack(bs->client);
		TAB_MoveforAttackQuad(bs, moveDir, Q_TL); trap->EA_Attack(bs->client);
		break;
	}
}

void Bot_ExecuteDefensiveCounter(bot_state_t* bs) {
	// Block and prepare counter
	int dist = bs->frame_Enemy_Len;

	// Range check - only execute if in range
	if (dist > 120) return;

	// trap->EA_Crouch(bs->client); // Niksata Edit

	if (BotEnemyInRecovery(bs->currentEnemy) && Q_irand(1, 10) < 7) {
		vec3_t moveDir;
		TAB_MoveforAttackQuad(bs, moveDir, Q_B);
		trap->EA_Move(bs->client, moveDir, 5000);
		trap->EA_Attack(bs->client);
	}
}

void Bot_ExecuteLungeAttack(bot_state_t* bs) {
	// Enhanced lunge with smart direction
	int dist = bs->frame_Enemy_Len;

	// Range check - only execute if in lunge range
	if (dist < 95 || dist > 140) return;

	vec3_t moveDir;
	trap->EA_MoveForward(bs->client);

	float rightDot = 0;
	vec3_t right;
	AngleVectors(bs->viewangles, NULL, right, NULL);
	vec3_t toEnemy;
	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->origin, toEnemy);
	VectorNormalize(toEnemy);
	rightDot = DotProduct(right, toEnemy);

	int quad = Q_B;
	if (rightDot > 0.3f) quad = Q_BR;
	else if (rightDot < -0.3f) quad = Q_BL;

	TAB_MoveforAttackQuad(bs, moveDir, quad);
	trap->EA_Move(bs->client, moveDir, 5000);
	trap->EA_Attack(bs->client);
}

void Bot_ExecuteKataAttack(bot_state_t* bs) {
	// WIDE range kata (40-256) and lower FP requirement (<=50)
	int dist = bs->frame_Enemy_Len;
	if (dist < 40 || dist > 256) return;

	// Check if bot has enough force power for kata
	if (bs->cur_ps.fd.forcePower < 50) {
		return; // Not enough force power for kata
	}

	// Must have saber equipped
	if (bs->cur_ps.weapon != WP_SABER) {
		return; // No saber equipped
	}

	// Check if bot is already in a kata animation
	// This prevents spamming kata commands while one is already executing
	if (BG_SaberInKata(bs->cur_ps.saberMove)) {
		return; // Already in kata - wait for it to complete
	}

	// Execute both attacks in immediate succession
	trap->EA_Attack(bs->client);
	trap->EA_Alt_Attack(bs->client);

	// Force immediate execution by clearing any delays
	g_entities[bs->client].client->ps.weaponTime = 0;

	// Track kata usage
	bs->lastKataTime = level.time;
	bs->kataStreak++;
} // Niksata Edit

// ================================ 
// Niksata Edit - ENHANCED WITH CALCULATEJUMP LOGIC
// SMART OBSTACLE INTERACTION (ENHANCED)
// ================================
qboolean Bot_CheckAndInteractWithObstacle(bot_state_t* bs) { // Niksata Edit - FIXED VERSION
	if (!bs) return qfalse;

	vec3_t forward, traceStart, traceEnd;
	trace_t tr;

	AngleVectors(bs->viewangles, forward, NULL, NULL);

	// Check for obstacles ahead
	VectorCopy(bs->origin, traceStart);
	traceStart[2] += 24;

	// NEW: EXTENDED DETECTION RANGE
	float detectionRange = 128.0f; // Increased from 64
	VectorMA(traceStart, detectionRange, forward, traceEnd);
	traceEnd[2] += 24;

	// FIXED: Use trace mask that excludes players
	trap->Trace(&tr, traceStart, NULL, NULL, traceEnd, bs->client,
		MASK_PLAYERSOLID & ~CONTENTS_BODY, qfalse, 0, 0);

	if (tr.entityNum < ENTITYNUM_WORLD && tr.fraction < 1.0f) {
		gentity_t* hitEnt = &g_entities[tr.entityNum];

		// CRITICAL FIX: Skip players and NPCs completely
		if (!hitEnt || !hitEnt->classname) return qfalse;
		if (hitEnt->client || hitEnt->s.NPC_class) return qfalse; // <-- MAIN FIX
		if (strstr(hitEnt->classname, "player") || strstr(hitEnt->classname, "npc")) return qfalse; // <-- EXTRA SAFETY

		// NEW: ENHANCED OBSTACLE CLASSIFICATION
		qboolean inCombat = (bs->currentEnemy && bs->frame_Enemy_Vis);
		qboolean isObstacle = qfalse;
		qboolean isWall = qfalse;
		qboolean isFence = qfalse;

		// Check obstacle types
		if (!Q_stricmp(hitEnt->classname, "func_breakable") ||
			!Q_stricmp(hitEnt->classname, "func_glass") ||
			!Q_stricmp(hitEnt->classname, "misc_model_breakable") ||
			!Q_stricmp(hitEnt->classname, "func_static") ||
			!Q_stricmp(hitEnt->classname, "misc_model") ||
			!Q_stricmp(hitEnt->classname, "misc_model_ghoul")) {

			isObstacle = qtrue;
		}

		// NEW: WALL DETECTION
		if (!Q_stricmp(hitEnt->classname, "func_wall") ||
			!Q_stricmp(hitEnt->classname, "func_static") ||
			!Q_stricmp(hitEnt->classname, "misc_model")) {
			isWall = qtrue;
			isObstacle = qtrue;
		}

		// NEW: FENCE DETECTION
		if (!Q_stricmp(hitEnt->classname, "func_fence") ||
			!Q_stricmpn(hitEnt->classname, "fence", 5) ||
			(hitEnt->model && strstr(hitEnt->model, "fence"))) {
			isFence = qtrue;
			isObstacle = qtrue;
		}

		// NEW: ENHANCED JUMP CALCULATION WITH CALCULATEJUMP LOGIC
				// ================================
		if (inCombat && isObstacle) {
			// CalculateJump logic integration
			vec3_t flatorigin, flatdest;
			float dist;
			float heightDif = hitEnt->r.absmax[2] - bs->origin[2];

			VectorCopy(bs->origin, flatorigin);
			VectorCopy(hitEnt->r.currentOrigin, flatdest);

			// Use flat distance calculation like original CalculateJump
			flatorigin[2] = flatdest[2] = 0;
			dist = Distance(flatdest, flatorigin);

			// Enhanced thresholds based on combat state (from CalculateJump analysis)
			float heightThreshold = 48.0f;  // Combat jump height
			float distanceThreshold = 150.0f; // Combat distance threshold

			// CalculateJump condition: heightDif > threshold && dist < threshold
			if (heightDif > heightThreshold && dist < distanceThreshold) {
				// Check if obstacle is jumpable (not too tall)
				vec3_t obstacleTop, jumpCheck;
				VectorCopy(hitEnt->r.absmin, obstacleTop);
				obstacleTop[2] = hitEnt->r.absmax[2];

				// Check if we can jump over it
				VectorCopy(bs->origin, jumpCheck);
				jumpCheck[2] += 48; // Jump height check

				// Check if obstacle is low enough to jump over
				if (obstacleTop[2] - bs->origin[2] < 48.0f) {
					// Check if we have room to jump
					trace_t jumpTrace;
					vec3_t jumpStart, jumpEnd;

					VectorCopy(bs->origin, jumpStart);
					jumpStart[2] += 24;

					VectorMA(jumpStart, 32.0f, forward, jumpEnd);
					jumpEnd[2] += 48; // Jump destination

					trap->Trace(&jumpTrace, jumpStart, NULL, NULL, jumpEnd, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

					// If we can jump over, do it
					if (jumpTrace.fraction >= 0.8f) {
						// Jump over obstacle
						trap->EA_Jump(bs->client);

						// Move forward while jumping
						trap->EA_Move(bs->client, forward, 4000);

						// Face enemy while jumping
						if (bs->currentEnemy) {
							vec3_t toEnemy;
							VectorSubtract(bs->currentEnemy->client->ps.origin, bs->origin, toEnemy);
							vectoangles(toEnemy, bs->ideal_viewangles);
						}

						return qtrue;
					}
				}
			}
		}

		// NEW: NON-COMBAT JUMP LOGIC (ORIGINAL CALCULATEJUMP BEHAVIOR)
		// ================================
		if (!inCombat && isObstacle) {
			// Original CalculateJump logic for non-combat situations
			vec3_t flatorigin, flatdest;
			float dist;
			float heightDif = hitEnt->r.absmax[2] - bs->origin[2];

			VectorCopy(bs->origin, flatorigin);
			VectorCopy(hitEnt->r.currentOrigin, flatdest);

			flatorigin[2] = flatdest[2] = 0;
			dist = Distance(flatdest, flatorigin);

			// Original CalculateJump thresholds
			if (heightDif > 30.0f && dist < 100.0f) {
				// Check if obstacle is jumpable
				if (hitEnt->r.absmax[2] - bs->origin[2] < 30.0f) {
					// Simple jump for non-combat
					trap->EA_Jump(bs->client);
					trap->EA_Move(bs->client, forward, 3000);
					return qtrue;
				}
			}
		}

		// SMART DOOR HANDLING
		if (!Q_stricmp(hitEnt->classname, "func_door")) {
			if (hitEnt->spawnflags & 1) {
				return qfalse;
			}

			float doorDist = Distance(bs->origin, hitEnt->r.currentOrigin);
			if (doorDist < 80.0f) {
				trap->EA_Use(bs->client);
				bs->noUseTime = level.time + 500;

				vec3_t toDoor;
				VectorSubtract(hitEnt->r.currentOrigin, bs->origin, toDoor);
				vectoangles(toDoor, bs->ideal_viewangles);

				return qtrue;
			}
			return qfalse;
		}

		// BUTTONS AND SWITCHES
		if (!Q_stricmp(hitEnt->classname, "func_button") ||
			!Q_stricmp(hitEnt->classname, "trigger_multiple") ||
			!Q_stricmp(hitEnt->classname, "target_switch")) {

			trap->EA_Use(bs->client);
			bs->noUseTime = level.time + 300;
			return qtrue;
		}

		// PLATFORMS AND ELEVATORS
		if (!Q_stricmp(hitEnt->classname, "func_plat") ||
			!Q_stricmp(hitEnt->classname, "func_train") ||
			!Q_stricmp(hitEnt->classname, "func_elevator")) {

			float platformHeight = fabs(hitEnt->r.currentOrigin[2] - bs->origin[2]);
			if (platformHeight < 32.0f) {
				trap->EA_Use(bs->client);
				bs->noUseTime = level.time + 500;
				return qtrue;
			}
		}

		// COMBAT OBSTACLE DESTRUCTION (NO JUMPING)
		if (inCombat && isObstacle) {
			if (bs->cur_ps.weapon == WP_SABER ||
				bs->cur_ps.weapon == WP_MELEE ||
				bs->cur_ps.weapon == WP_ROCKET_LAUNCHER ||
				bs->cur_ps.weapon == WP_THERMAL ||
				bs->cur_ps.weapon == WP_DET_PACK) {

				trap->EA_Attack(bs->client);
				return qtrue;
			}
		}

		// SIMPLE OBSTACLE AVOIDANCE (NO JUMPING)
		if (inCombat && isObstacle) {
			vec3_t left, right, avoidDir;
			AngleVectors(bs->viewangles, NULL, left, right);
			VectorScale(left, -1.0f, left);

			// Check left path
			VectorMA(bs->origin, 64.0f, left, traceStart);
			traceStart[2] += 24;
			VectorMA(traceStart, 64.0f, forward, traceEnd);
			traceEnd[2] += 24;

			trap->Trace(&tr, traceStart, NULL, NULL, traceEnd, bs->client, MASK_PLAYERSOLID & ~CONTENTS_BODY, qfalse, 0, 0);
			qboolean leftClear = (tr.fraction >= 0.8f);

			// Check right path
			VectorMA(bs->origin, 64.0f, right, traceStart);
			traceStart[2] += 24;
			VectorMA(traceStart, 64.0f, forward, traceEnd);
			traceEnd[2] += 24;

			trap->Trace(&tr, traceStart, NULL, NULL, traceEnd, bs->client, MASK_PLAYERSOLID & ~CONTENTS_BODY, qfalse, 0, 0);
			qboolean rightClear = (tr.fraction >= 0.8f);

			// Choose best avoidance direction
			if (leftClear && !rightClear) {
				VectorCopy(left, avoidDir);
			}
			else if (rightClear && !leftClear) {
				VectorCopy(right, avoidDir);
			}
			else if (leftClear && rightClear) {
				if (Q_irand(0, 1)) {
					VectorCopy(left, avoidDir);
				}
				else {
					VectorCopy(right, avoidDir);
				}
			}
			else {
				VectorScale(forward, -1.0f, avoidDir);
			}

			trap->EA_Move(bs->client, avoidDir, 3000);

			if (bs->currentEnemy) {
				vec3_t toEnemy;
				VectorSubtract(bs->currentEnemy->client->ps.origin, bs->origin, toEnemy);
				vectoangles(toEnemy, bs->ideal_viewangles);
			}

			return qtrue;
		}
	}

	return qfalse;
} // Niksata Edit - FIXED VERSION: No player detection, no jump spam

// Enhanced saber combat execution // Niksata Edit
void Bot_ExecuteSaberCombat(bot_state_t* bs) {
	int dist = bs->frame_Enemy_Len;
	vec3_t dir, moveDir;
	vec3_t mins, maxs, forward, right, up, end;
	trace_t tr;
	float wallDistance;
	qboolean nearWall;

	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, dir);
	vectoangles(dir, bs->ideal_viewangles);

	// ================================
	// WALL DETECTION SYSTEM
	// ================================

	// CHECK IF NEAR WALL BEFORE COMBAT ACTIONS
	VectorSet(mins, -15, -15, -8);
	VectorSet(maxs, 15, 15, 32);

	AngleVectors(bs->viewangles, forward, right, up);

	// Check forward distance to wall
	VectorMA(bs->origin, 48, forward, end);
	JP_Trace(&tr, bs->origin, mins, maxs, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);
	wallDistance = tr.fraction * 48;

	// If too close to wall, disable crouching and adjust combat
	nearWall = (wallDistance < 24);

	// When near wall, prefer backing up and strafing
	if (nearWall)
	{
		// 50% chance to back up from wall
		if (Q_irand(1, 10) <= 5)
		{
			trap->EA_MoveBack(bs->client);
		}

		// Add more aggressive turning to find open space
		bs->ideal_viewangles[YAW] += Q_irand(-30, 30);
	}

	// ================================
	// EMERGENCY SABER ACTIVATION (FORCE FIGHTING)
	// ================================

	// Force bot to activate saber and fight if enemy is very close (regardless of saber state)
	if (bs->cur_ps.saberHolstered && dist <= 60)
	{
		// Emergency saber activation
		Cmd_ToggleSaber_f(&g_entities[bs->client]);

		// Immediate attack to force engagement
		trap->EA_Attack(bs->client);
		return;
	}

	// ================================
	// FORCED ATTACK SYSTEM - GUARANTEED ATTACKS AT VERY CLOSE RANGE
	// ================================

	// Increase attack frequency when near wall (more aggressive to escape)
	if (nearWall && Q_irand(1, 10) <= 8) // 80% attack chance near walls
	{
		if (bs->frame_Enemy_Len <= SABER_ATTACK_RANGE)
		{
			bs->doAttack = 1;
		}
	}
	else if (Q_irand(1, 10) <= 7) // 70% chance to attack (increased from default)
	{
		if (bs->frame_Enemy_Len <= SABER_ATTACK_RANGE)
		{
			bs->doAttack = 1;
		}
	}

	// Add special moves more frequently
	if (Q_irand(1, 10) <= 4) // 40% chance for special moves
	{
		// Your existing special move logic here
	}

	if (bs->currentEnemy && bs->frame_Enemy_Vis) // Niksata Edit
	{
		//Bot_CheckAndInteractWithObstacle(bs, bs->currentEnemy->client->ps.origin);

		// Check for obstacles between bot and enemy
		qboolean shouldJumpOverObstacle = qfalse;
		vec3_t jumpDir;

		if (bs->currentEnemy && bs->frame_Enemy_Vis && dist <= 200) {
			vec3_t toEnemy, traceStart, traceEnd;
			trace_t obstacleTrace;

			VectorSubtract(bs->currentEnemy->client->ps.origin, bs->origin, toEnemy);
			VectorNormalize(toEnemy);

			// Check for obstacles in path to enemy
			VectorCopy(bs->origin, traceStart);
			traceStart[2] += 24;
			VectorMA(traceStart, dist, toEnemy, traceEnd);
			traceEnd[2] += 24;

			trap->Trace(&obstacleTrace, traceStart, NULL, NULL, traceEnd, bs->client,
				MASK_PLAYERSOLID & ~CONTENTS_BODY, qfalse, 0, 0);

			// If obstacle detected and it's jumpable
			if (obstacleTrace.fraction < 0.8f && obstacleTrace.entityNum < ENTITYNUM_WORLD) {
				gentity_t* obstacle = &g_entities[obstacleTrace.entityNum];

				if (obstacle && obstacle->r.absmax[2] - bs->origin[2] < 64.0f) {
					// Check if we can jump over it
					vec3_t jumpCheckStart, jumpCheckEnd;
					VectorCopy(bs->origin, jumpCheckStart);
					jumpCheckStart[2] += 24;

					VectorMA(jumpCheckStart, 48.0f, toEnemy, jumpCheckEnd);
					jumpCheckEnd[2] += 48;

					trace_t jumpTrace;
					trap->Trace(&jumpTrace, jumpCheckStart, NULL, NULL, jumpCheckEnd, bs->client,
						MASK_PLAYERSOLID, qfalse, 0, 0);

					if (jumpTrace.fraction >= 0.7f) {
						shouldJumpOverObstacle = qtrue;
						VectorCopy(toEnemy, jumpDir);
					}
				}
			}
		}

		// Execute jump while continuing combat
		if (shouldJumpOverObstacle) {
			trap->EA_Jump(bs->client);
			trap->EA_Move(bs->client, jumpDir, 4000);

			// Attack while jumping
			if (Q_irand(1, 100) <= 70) {
				trap->EA_Attack(bs->client);
			}

			// Face enemy while jumping
			if (bs->currentEnemy) {
				vec3_t toEnemy;
				VectorSubtract(bs->currentEnemy->client->ps.origin, bs->origin, toEnemy);
				vectoangles(toEnemy, bs->ideal_viewangles);
			}

			// Don't return - continue with normal combat logic
		} // Niksata Edit

		// If we're actively avoiding, skip some combat actions
		if (bs->avoidanceTimer > level.time)
		{
			// Continue avoidance but don't do complex combat maneuvers
			if (bs->avoidanceDirection > 0)
				trap->EA_MoveRight(bs->client);
			else if (bs->avoidanceDirection < 0)
				trap->EA_MoveLeft(bs->client);

			// Still attack if possible, but prioritize movement
			if (Q_irand(0, 3) == 0 && bs->frame_Enemy_Vis)
				trap->EA_Attack(bs->client);

			return; // Skip rest of combat logic while avoiding
		}
	}

	// ================================
	// DYNAMIC RANGES BASED ON SABER STYLE (CLOSER COMBAT)
	// ================================

	// Dynamic ranges based on saber style
	int SABER_RANGE, CLOSE_RANGE, LUNGE_MIN, LUNGE_MAX, SPECIAL_RANGE;

	// Adjust ranges based on current saber style
	switch (bs->cur_ps.fd.saberAnimLevel) {
	case SS_FAST: // Blue style - very close range
		SABER_RANGE = 50;    // Reduced from 60
		CLOSE_RANGE = 32;     // Reduced from 40
		LUNGE_MIN = 60;       // Reduced from 70
		LUNGE_MAX = 90;       // Reduced from 110
		SPECIAL_RANGE = 150;  // Reduced from 180
		break;

	case SS_MEDIUM: // Yellow style - close-medium range
		SABER_RANGE = 60;     // Reduced from 70
		CLOSE_RANGE = 40;     // Reduced from 48
		LUNGE_MIN = 75;       // Reduced from 85
		LUNGE_MAX = 105;      // Reduced from 125
		SPECIAL_RANGE = 190;   // Reduced from 220
		break;

	case SS_STRONG: // Red style - medium range
		SABER_RANGE = 70;     // Reduced from 80
		CLOSE_RANGE = 45;     // Reduced from 54
		LUNGE_MIN = 85;       // Reduced from 95
		LUNGE_MAX = 120;      // Reduced from 140
		SPECIAL_RANGE = 220;  // Reduced from 256
		break;

	case SS_DESANN: // Desann style - medium-long range
		SABER_RANGE = 75;     // Reduced from 85
		CLOSE_RANGE = 50;     // Reduced from 58
		LUNGE_MIN = 90;       // Reduced from 100
		LUNGE_MAX = 130;      // Reduced from 150
		SPECIAL_RANGE = 240;  // Reduced from 280
		break;

	case SS_TAVION: // Tavion style - fast-medium
		SABER_RANGE = 55;     // Reduced from 65
		CLOSE_RANGE = 36;     // Reduced from 44
		LUNGE_MIN = 68;       // Reduced from 78
		LUNGE_MAX = 98;       // Reduced from 118
		SPECIAL_RANGE = 170;  // Reduced from 200
		break;

	default: // Fallback to medium values
		SABER_RANGE = 60;
		CLOSE_RANGE = 40;
		LUNGE_MIN = 75;
		LUNGE_MAX = 105;
		SPECIAL_RANGE = 190;
		break;
	}

	// ================================
	// SPECIAL MOVE STATE MANAGEMENT (ANTI-JITTER)
	// ================================

	// Define special move types
	enum {
		SPECIAL_NONE = 0,
		SPECIAL_KATA = 1,
		SPECIAL_DANCE = 2,
		SPECIAL_RUSH = 3,
		SPECIAL_HURRICANE = 4,
		SPECIAL_STYLE_SWITCH = 5,
		SPECIAL_DELAY_ATTACK = 6,
		SPECIAL_CONVERGENT = 7,
		SPECIAL_STATIC = 8,
		SPECIAL_COMBO = 9,
		SPECIAL_DFA = 10,
		SPECIAL_CROUCHLUNGE = 11
	};

	// Check if current special should expire
	if (bs->currentSpecial != SPECIAL_NONE && (level.time - bs->lastSpecialTime >= bs->specialDuration)) {
		bs->currentSpecial = SPECIAL_NONE;
	}

	// Don't start new specials if one is active or on cooldown
	if (bs->currentSpecial != SPECIAL_NONE || (level.time - bs->lastSpecialTime < 600)) { // Reduced cooldown from 800 to 600
		// Only continue movement for current special, don't start new ones
		if (bs->currentSpecial != SPECIAL_NONE) {
			// Continue current special movement (no new commands)
			return;
		}
		// On cooldown - allow normal attacks but no specials
	}

	// ================================
	// REMOVED: OBSTACLE CHECK BEFORE COMBAT
	// ================================
	// FIXED: No obstacle checking in combat - this was causing jump spam

	// ================================
	// RANGE VALIDATION - DYNAMIC RANGES
	// ================================

	// Early return if completely out of combat range
	if (dist > SPECIAL_RANGE) {
		return; // Don't attack if too far
	}

	// ================================
	// UPDATE SWING MEMORY FIRST
	// ================================
	Bot_UpdateEnemySwingMemory(bs);

	// ================================
	// BALANCED BLOCKING/ATTACKING (REDUCED EARLY RETURNS - MORE AGGRESSIVE)
	// ================================

	qboolean enemyRecovering = BotEnemyInRecovery(bs->currentEnemy);
	qboolean enemyVulnerable = BotEnemyVulnerable(bs->currentEnemy);
	qboolean enemySwinging = BotEnemySwinging(bs->currentEnemy);

	// Perfect blocking but allow attacking (less restrictive)
	Bot_ExecutePerfectBlocking(bs);

	// ONLY BLOCK IF ENEMY IS ACTIVELY ATTACKING AND WE HAVE LOW CONFIDENCE (MORE AGGRESSIVE)
	qboolean shouldBlock = enemySwinging &&
		(bs->blocking.blockConfidence < 0.2f || // Reduced from 0.3f to 0.2f - even more aggressive
			bs->blocking.enemyAttackPower > 18.0f); // Increased from 15.0f to 18.0f

	if (shouldBlock &&
		(bs->blocking.enemySwingStage >= 2 && bs->blocking.enemySwingStage <= 6)) {
		// Block but prepare to counter - INCREASED COUNTER CHANCE
		if (enemyRecovering && Q_irand(1, 100) <= 80) { // Increased from 60% to 80%
			// FIXED: Removed obstacle check
			vec3_t counterDir;
			TAB_MoveforAttackQuad(bs, counterDir, Q_B);
			trap->EA_Move(bs->client, counterDir, 6000);
			trap->EA_Attack(bs->client);
			return;
		}
		return; // Block this frame
	}

	// ================================
	// SPECIAL ATTACK PRIORITY SYSTEM (INCREASED BASE CHANCE)
	// ================================

	// Calculate special attack priority based on situation
	int specialPriority = 0;

	// Increase priority based on confidence and situation
	if (bs->blocking.blockConfidence > 0.4f) specialPriority += 25; // Reduced from 0.5f to 0.4f, increased from 20 to 25
	if (enemyVulnerable) specialPriority += 30; // Increased from 25 to 30
	if (enemyRecovering) specialPriority += 20; // Increased from 15 to 20
	if (bs->currentEnemy->health < g_entities[bs->client].health) specialPriority += 15; // Increased from 10 to 15
	if (dist <= 100) specialPriority += 20; // Increased from 15 to 20

	// Add swing memory bonus for tactical positioning
	if (bs->swingMemoryLeft > 3.0f) specialPriority += 8; // Increased from 5 to 8
	if (bs->swingMemoryRight > 3.0f) specialPriority += 8; // Increased from 5 to 8
	if (bs->swingMemoryBack > 2.0f) specialPriority += 5; // Increased from 3 to 5

	// Base special attack chance with priority bonus (INCREASED)
	int specialChance = 65 + specialPriority; // Increased from 50 to 65
	if (specialChance > 95) specialChance = 95; // Increased from 85 to 95

	// ================================
	// SPECIAL ATTACKS - WITH STATE MANAGEMENT (NO OBSTACLE CHECKS)
	// ================================

	// KATA ATTACK (45% + priority) - DYNAMIC RANGE
	if (dist <= SPECIAL_RANGE && Q_irand(1, 100) <= (specialChance + 10)) { // Increased from 35% to 45%, bonus from 7 to 10
		// Set state and execute
		bs->currentSpecial = SPECIAL_KATA;
		bs->specialDuration = 1200; // 1.2 seconds
		bs->lastSpecialTime = level.time;

		// FIXED: Removed obstacle check
		Bot_ExecuteKataAttack(bs);
		return;
	}

	// SABER DANCE (43% + priority) - REDUCED GATING
	if (dist <= CLOSE_RANGE && Q_irand(1, 100) <= (specialChance + 8)) { // Increased from 33% to 43%, bonus from 5 to 8
		bs->currentSpecial = SPECIAL_DANCE;
		bs->specialDuration = 800; // 0.8 seconds
		bs->lastSpecialTime = level.time;

		// FIXED: Removed obstacle check
		Bot_ExecuteSaberDance(bs);
		return;
	}

	// RUSH ATTACK (40% + priority) - REDUCED GATING
	if (dist <= 100 && Q_irand(1, 100) <= (specialChance + 5)) { // Increased from 30% to 40%, bonus from 0 to 5
		bs->currentSpecial = SPECIAL_RUSH;
		bs->specialDuration = 600; // 0.6 seconds
		bs->lastSpecialTime = level.time;

		// FIXED: Removed obstacle check
		Bot_ExecuteRushAttack(bs);
		return;
	}

	// DFA ATTACK (40% + priority) - REDUCED GATING
	if (dist <= 200 && Q_irand(1, 100) <= (specialChance + 5)) { // Increased from 30% to 40%, bonus from 0 to 5

		// CHECK SABER STYLE - only for MEDIUM, STRONG, DUAL, STAFF
		if (bs->cur_ps.fd.saberAnimLevel == SS_MEDIUM ||
			bs->cur_ps.fd.saberAnimLevel == SS_STRONG ||
			bs->cur_ps.fd.saberAnimLevel == SS_DUAL ||
			bs->cur_ps.fd.saberAnimLevel == SS_STAFF) {

			bs->currentSpecial = SPECIAL_DFA;
			bs->specialDuration = 1000; // 1.0 seconds
			bs->lastSpecialTime = level.time;

			// FIXED: Removed obstacle check
			Bot_ExecuteDFA(bs);
			return;
		}
	}

	// Crouch Lunge ATTACK (40% + priority) - REDUCED GATING
	if (dist <= 100 && Q_irand(1, 100) <= (specialChance + 5)) { // Increased from 30% to 40%, bonus from 0 to 5

		// CHECK SABER STYLE - only for FAST, DUAL, STAFF
		if (bs->cur_ps.fd.saberAnimLevel == SS_FAST ||
			bs->cur_ps.fd.saberAnimLevel == SS_DUAL ||
			bs->cur_ps.fd.saberAnimLevel == SS_STAFF) {

			bs->currentSpecial = SPECIAL_CROUCHLUNGE;
			bs->specialDuration = 1000; // 1.0 seconds
			bs->lastSpecialTime = level.time;

			// FIXED: Removed obstacle check
			Bot_ExecuteCrouchLunge(bs);
			return;
		}
	}

	// HURRICANE (37% + priority) - REDUCED GATING
	if (dist <= 90 && Q_irand(1, 100) <= (specialChance + 2)) { // Increased from 27% to 37%, bonus from -3 to 2
		bs->currentSpecial = SPECIAL_HURRICANE;
		bs->specialDuration = 700; // 0.7 seconds
		bs->lastSpecialTime = level.time;

		// FIXED: Removed obstacle check
		Bot_ExecuteHurricane(bs);
		return;
	}

	// STYLE SWITCH DAMAGE BOOST (35% + priority) - REDUCED GATING
	if (dist <= 120 && enemyVulnerable && Q_irand(1, 100) <= (specialChance)) { // Increased from 25% to 35%, bonus from -5 to 0
		bs->currentSpecial = SPECIAL_STYLE_SWITCH;
		bs->specialDuration = 400; // 0.4 seconds
		bs->lastSpecialTime = level.time;

		// FIXED: Removed obstacle check
		Bot_ExecuteStyleSwitch(bs);
		return;
	}

	// A-STRIKE DELAY (42% + priority) - REDUCED GATING
	if (dist <= 110 && enemyVulnerable && Q_irand(1, 100) <= (specialChance + 6)) { // Increased from 32% to 42%, bonus from 3 to 6
		bs->currentSpecial = SPECIAL_DELAY_ATTACK;
		bs->specialDuration = 900; // 0.9 seconds
		bs->lastSpecialTime = level.time;

		// FIXED: Removed obstacle check
		Bot_ExecuteDelayAttack(bs);
		return;
	}

	// CONVERGENT ATTACK (48% + priority) - REDUCED GATING
	if (dist <= 100 && Q_irand(1, 100) <= (specialChance + 15)) { // Increased from 38% to 48%, bonus from 10 to 15
		bs->currentSpecial = SPECIAL_CONVERGENT;
		bs->specialDuration = 800; // 0.8 seconds
		bs->lastSpecialTime = level.time;

		// FIXED: Removed obstacle check
		Bot_ExecuteConvergentAttack(bs);
		return;
	}

	// STATIC ATTACK (43% + priority) - REDUCED GATING
	if (dist <= 90 && enemyRecovering && Q_irand(1, 100) <= (specialChance + 8)) { // Increased from 33% to 43%, bonus from 5 to 8
		bs->currentSpecial = SPECIAL_STATIC;
		bs->specialDuration = 600; // 0.6 seconds
		bs->lastSpecialTime = level.time;

		// FIXED: Removed obstacle check
		Bot_ExecuteStaticAttack(bs);
		return;
	}

	// THREE-SWING COMBOS (40% + priority) - REDUCED GATING
	if (dist <= 120 && Q_irand(1, 100) <= (specialChance + 5)) { // Increased from 30% to 40%, bonus from 0 to 5
		bs->currentSpecial = SPECIAL_COMBO;
		bs->specialDuration = 1000; // 1.0 seconds
		bs->lastSpecialTime = level.time;

		// FIXED: Removed obstacle check
		Bot_ExecuteThreeSwingCombo(bs);
		return;
	}

	// ================================
	// AGGRESSIVE ATTACK PRIORITIES (INCREASED BASE CHANCE) - NO OBSTACLE CHECKS
	// ================================

	// HIGH AGGRESSION - Default attack patterns (INCREASED from 50% to 70%)
	if (dist <= SABER_RANGE && Q_irand(1, 100) <= 70) { // Increased from 50% to 70%
		// FIXED: Removed obstacle check
		// REMOVED SABER MOVE STATE CHECK - ALWAYS ATTACK
		int quad = Q_irand(Q_R, Q_L);
		TAB_MoveforAttackQuad(bs, moveDir, quad);
		trap->EA_Move(bs->client, moveDir, 5000);
		trap->EA_Attack(bs->client);
		return;
	}

	// ================================
	// SITUATIONAL ATTACKS (More aggressive) - NO OBSTACLE CHECKS
	// ================================

	// DEFENSIVE BLOCK AND COUNTER (More aggressive counter - INCREASED from 75% to 90%)
	if (enemySwinging && dist < SABER_RANGE) {
		if (bs->blocking.blockConfidence > 0.2f && Q_irand(1, 100) <= 90) { // Increased from 75% to 90%, reduced confidence from 0.3f to 0.2f
			// FIXED: Removed obstacle check
			Bot_ExecuteDefensiveCounter(bs);
			return;
		}
		// Otherwise block normally
		return;
	}

	// LUNGE ATTACKS (Increased base chance)
	if (dist >= LUNGE_MIN && dist <= LUNGE_MAX && enemyVulnerable) {
		float lungeChance = 0.65f; // Increased from 0.45f to 0.65f
		if (bs->currentEnemy->health > g_entities[bs->client].health) {
			lungeChance = 0.85f; // Increased from 0.65f to 0.85f
		}
		if (Q_flrand(0, 1) < lungeChance) {
			// FIXED: Removed obstacle check
			Bot_ExecuteLungeAttack(bs);
			return;
		}
	}

	// ================================
	// CLOSE RANGE COMBAT (Much more aggressive) - NO OBSTACLE CHECKS
	// ================================

	if (dist <= CLOSE_RANGE) {
		// REMOVED HIT SPOTTED CHECK - ALWAYS ATTACK AT CLOSE RANGE

		// FIXED: Removed obstacle check
		// REMOVED SABER MOVE STATE CHECK - ALWAYS ATTACK
		// Much higher attack chance at close range (INCREASED from 80% to 98%)
		if (Q_irand(1, 100) <= 98) { // Increased from 95% to 98%
			int quad = enemyRecovering ? Q_irand(Q_BR, Q_B) : Q_irand(Q_R, Q_L);
			TAB_MoveforAttackQuad(bs, moveDir, quad);
			trap->EA_Move(bs->client, moveDir, 5000);
			trap->EA_Attack(bs->client);
			return;
		}
		return;
	}

	// ================================
	// EDGE PRESSURE (More aggressive) - NO OBSTACLE CHECKS
	// ================================

	if (dist <= SABER_RANGE) {
		// FIXED: Removed obstacle check
		float pressureChance = 0.85f; // Increased from 0.65f to 0.85f
		if (bs->currentEnemy->health > g_entities[bs->client].health * 1.1f) {
			pressureChance = 0.95f; // Increased from 0.8f to 0.95f
		}
		if (Q_flrand(0, 1) < pressureChance) {
			int attackPattern = Q_irand(0, 2);
			int quad;

			switch (attackPattern) {
			case 0: quad = Q_irand(Q_BR, Q_B); break;
			case 1: quad = Q_irand(Q_R, Q_L); break;
			case 2: quad = Q_T; break;
			}

			TAB_MoveforAttackQuad(bs, moveDir, quad);
			trap->EA_Move(bs->client, moveDir, 5000);
			trap->EA_Attack(bs->client);
		}
	}
} // Niksata Edit

void NewBotAI_GetAttack(bot_state_t* bs) // Niksata Edit
{
	int weapon;
	// const float speed = NewBotAI_GetSpeedTowardsEnemy(bs);

	if (!bs->client || !bs->currentEnemy || !bs->currentEnemy->client)
		return;

	if (g_tweakWeapons.integer & WT_TRIBES)
		weapon = NewBotAI_GetTribesWeapon(bs);
	else
		weapon = NewBotAI_GetWeapon(bs);
	BotSelectWeapon(bs->client, weapon);

	if (bs->runningLikeASissy) //Dont attack when chasing them with strafe i guess
		return;
	if (bs->isCamper && (bs->cur_ps.weaponstate != WEAPON_CHARGING_ALT) && (bs->cur_ps.weaponstate != WEAPON_CHARGING)) {//don't attack if waiting for them to land to groundpound - modify this so we don't cancel a charge
		return;
	}

	if (bs->currentEnemy->client->invulnerableTimer && (bs->currentEnemy->client->invulnerableTimer > level.time)) {//don't attack them if they can't take dmg
		return;
	}

	// --- SABER ATTACK LOGIC (IMPROVED + QUADS + LUNGES + FEINTS) --- // Niksata Edit
	if (bs->cur_ps.weapon == WP_SABER)
	{	// ================================
		// YOUR INTELLIGENT STYLE SELECTION
		// ================================
		Bot_SelectOptimalSaberStyle(bs);

		// Use enhanced saber combat system
		Bot_ExecuteSaberCombat(bs);
		return;
	} // Niksata Edit

	if (!bs->frame_Enemy_Vis && (bs->cur_ps.weapon != WP_DEMP2)) { //Dont waste ammo if we cant see them..?
		return;
	}

	if (!bs->isCamper && (bs->cur_ps.weapon == weapon)) {
		// Check enemy vulnerability for timing
		qboolean enemyVulnerable = BotEnemyVulnerable(bs->currentEnemy);

		if (bs->doAltAttack) {
			const int altCharge = NewBotAI_GetAltCharge(bs);
			if (weapon == WP_STUN_BATON && bs->frame_Enemy_Len > 240 && (g_tweakWeapons.integer & WT_STUN_SHOCKLANCE)) { //Weird case for stun baton since low range and low firerate, dont bother until they are in range
			}
			else if (altCharge == 1) {
				trap->EA_Alt_Attack(bs->client);
			}
			else if (altCharge == 3) {
				// Fire more when enemy is vulnerable
				int fireRate = enemyVulnerable ? 1 : 2;
				if (level.framenum % fireRate)
					trap->EA_Alt_Attack(bs->client);
			}
		}
		else {
			const int charge = NewBotAI_GetCharge(bs);
			if (charge == 1) { //0 is no ammo, 1 charging, 2 is release charge, 3 is not charging
				trap->EA_Attack(bs->client);
			}
			else if (charge == 3) {
				// Fire more when enemy is vulnerable
				int fireRate = enemyVulnerable ? 1 : 2;
				if (level.framenum % fireRate)
					trap->EA_Attack(bs->client);
			}
		}
	}
} // Niksata Edit

void NewBotAI_GetGroundDodge(bot_state_t* bs) {
	bs->runningLikeASissy = 0;

	if (bs->forceMove_Right > 0)
		trap->EA_MoveRight(bs->client);
	else if (bs->forceMove_Right < 0)
		trap->EA_MoveLeft(bs->client);

	if (level.time % 1500 > 750) {
		if (Q_flrand(0.0f, 1.0f) > 0.5)
			bs->forceMove_Right = 1;
		else
			bs->forceMove_Right = -1;
	}
}



void Bot_ClearForcedMovement(bot_state_t* bs) { // Niksata Edit
	if (!bs) return;

	bs->forceMove_Forward = 0;
	bs->forceMove_Right = 0;
	bs->forceMove_Up = 0;
} // Niksata Edit

// ================================
// Niksata Edit
// CHOOSE BEST DODGE DIR FROM MEMORY
// ================================
// ENHANCED VERSION WITH PREDICTIVE DODGING
int Bot_GetAdaptiveDodgeDir(bot_state_t* bs) {
	if (!bs) return 0;

	// Calculate swing dominance
	float totalSwings = bs->swingMemoryLeft + bs->swingMemoryRight + bs->swingMemoryBack;
	if (totalSwings < 0.5f) return 0; // Not enough data

	// ENHANCED: Add recent swing weighting
	float leftProb = (bs->swingMemoryLeft * 0.7f) + (bs->recentSwingMemoryLeft * 0.3f);
	float rightProb = (bs->swingMemoryRight * 0.7f) + (bs->recentSwingMemoryRight * 0.3f);
	float backProb = (bs->swingMemoryBack * 0.7f) + (bs->recentSwingMemoryBack * 0.3f);

	// ENHANCED: Add enemy movement prediction
	vec3_t forward, enemyVel;
	AngleVectors(bs->viewangles, forward, NULL, NULL);

	if (bs->lastEnemyTime > 0) {
		VectorSubtract(bs->currentEnemy->client->ps.origin, bs->lastEnemyPos, enemyVel);
		VectorScale(enemyVel, 1.0f / (level.time - bs->lastEnemyTime), enemyVel);

		// Adjust probabilities based on enemy movement
		float enemyDot = DotProduct(enemyVel, forward);
		if (enemyDot > 50) { // Enemy moving forward
			backProb += 0.2f; // More likely to dodge back
		}
		else if (enemyDot < -50) { // Enemy moving back
			leftProb += 0.1f;
			rightProb += 0.1f; // More likely to dodge sideways
		}
	}

	// ENHANCED: Add health-based adjustment
	if (g_entities[bs->client].health < 50) {
		backProb += 0.15f; // Low health - more defensive
	}

	// Add randomness to avoid predictability
	float randomFactor = Q_flrand(-0.2f, 0.2f);
	leftProb += randomFactor;
	rightProb += randomFactor;
	backProb += randomFactor;

	// Find highest probability
	if (leftProb > rightProb && leftProb > backProb) {
		return -1; // Dodge left
	}
	else if (rightProb > leftProb && rightProb > backProb) {
		return 1;  // Dodge right
	}
	else if (backProb > leftProb && backProb > rightProb) {
		return 2;  // Dodge back
	}

	// If close, add random choice
	return Q_irand(-1, 1);
} // Niksata Edit

// ================================
// Niksata Edit
// SMART TRAP FOOTWORK
// ================================
void Bot_TrapSetup(bot_state_t* bs) {
	if (!bs) return;

	float r = Q_flrand(0.0f, 1.0f);

	if (r < 0.25f) trap->EA_MoveLeft(bs->client);
	else if (r < 0.5f) trap->EA_MoveRight(bs->client);
	else if (r < 0.75f) trap->EA_MoveForward(bs->client);
	else trap->EA_MoveBack(bs->client);

	/*if (Q_irand(0, 14) == 0)
		trap->EA_Crouch(bs->client); // fake timing bait*/
} // Niksata Edit

qboolean Bot_ShouldBackstepForSwing(bot_state_t* bs) {
	if (!bs || !bs->currentEnemy) return qfalse;

	int move = bs->currentEnemy->client->ps.saberMove;
	int timer = bs->currentEnemy->client->ps.torsoTimer;
	float dist = bs->frame_Enemy_Len;

	if (move == LS_NONE || move == LS_READY)
		return qfalse;

	// Predict hit window
	if (timer < 180 && timer > 60) {   // Mid swing frames
		if (dist < 105)                // Inside saber range
			return qtrue;
	}

	return qfalse;
} // Niksata Edit

// ================================
// Niksata Edit
// Helper: Ultra-Instinct Aim
// ================================
void Bot_ApplyUltraInstinctAim(bot_state_t* bs, float strength) {
	if (!bs) return;

	bs->ideal_viewangles[YAW] += Q_flrand(-strength, strength);
	bs->ideal_viewangles[PITCH] += Q_flrand(-strength * 0.4f, strength * 0.4f);
} // Niksata Edit

int Bot_GetMixedAttackMove() { // Niksata Edit
	int r = Q_irand(0, 4);

	switch (r) {
	case 0: return LS_A_L2R;
	case 1: return LS_A_R2L;
	case 2: return LS_A_T2B;
	case 3: return LS_A_TR2BL;
	case 4: return LS_A_TL2BR;
	default: return LS_A_BL2TR;
	}
} // Niksata Edit

qboolean Bot_ShouldPressAttack(bot_state_t* bs, int dist) { // Niksata Edit
	if (!bs || !bs->currentEnemy) return qfalse;

	// Enemy recovering or idle
	if (bs->currentEnemy->client->ps.saberMove == LS_READY ||
		bs->currentEnemy->client->ps.torsoTimer < 120)
		return (dist < 130);

	return qfalse;
} // Niksata Edit

// ================================
// Niksata Edit
// Helper: Dodge Swing Arc
// ================================
void Bot_EvadeSwingArc(bot_state_t* bs, int dist) {
	if (!bs) return;

	if (dist < 110)
		trap->EA_MoveBack(bs->client);
	else if (dist > 180)
		trap->EA_MoveForward(bs->client);

	if (Q_flrand(0, 1) < 0.3f) {
		if (Q_irand(0, 1)) trap->EA_MoveLeft(bs->client);
		else trap->EA_MoveRight(bs->client);
	}
} // Niksata Edit

void Bot_CheckRearThreat(bot_state_t* bs, float frontDot) // Niksata Edit
{
	if (frontDot < -0.6f) { // enemy behind
		trap->EA_MoveForward(bs->client); // escape forward
		bs->ideal_viewangles[YAW] += 180; // snap turn
	}
} // Niksata Edit

// ================================
// ENHANCED OBSTACLE-AWARE MOVEMENT
// ================================
qboolean Bot_CheckMovementPaths(bot_state_t* bs) {
	if (!bs) return qfalse;

	vec3_t forward, left, right;
	AngleVectors(bs->viewangles, forward, right, left);
	VectorScale(right, -1.0f, right);

	// Check multiple directions for obstacles
	qboolean frontBlocked = qfalse;
	qboolean leftBlocked = qfalse;
	qboolean rightBlocked = qfalse;

	trace_t tr;
	vec3_t start, end;

	// Check forward path
	VectorCopy(bs->origin, start);
	start[2] += 24;
	VectorMA(start, 64.0f, forward, end);
	end[2] += 24;
	trap->Trace(&tr, start, NULL, NULL, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);
	if (tr.fraction < 0.8f) frontBlocked = qtrue;

	// Check left path
	VectorMA(start, 32.0f, left, end);
	trap->Trace(&tr, start, NULL, NULL, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);
	if (tr.fraction < 0.8f) leftBlocked = qtrue;

	// Check right path
	VectorMA(start, 32.0f, right, end);
	trap->Trace(&tr, start, NULL, NULL, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);
	if (tr.fraction < 0.8f) rightBlocked = qtrue;

	// ENHANCED: Intelligent path selection (BALANCED JUMP USAGE)
	if (frontBlocked) {
		if (!leftBlocked && !rightBlocked) {
			// Both sides clear - choose based on enemy position
			vec3_t toEnemy;
			if (bs->currentEnemy && bs->currentEnemy->client) {
				VectorSubtract(bs->currentEnemy->client->ps.origin, bs->origin, toEnemy);
				float enemyDot = DotProduct(toEnemy, right);

				if (enemyDot > 0) {
					trap->EA_MoveRight(bs->client);
				}
				else {
					trap->EA_MoveLeft(bs->client);
				}
			}
			else {
				// No enemy - pick random direction
				if (Q_irand(0, 1)) trap->EA_MoveLeft(bs->client);
				else trap->EA_MoveRight(bs->client);
			}
		}
		else if (!leftBlocked) {
			trap->EA_MoveLeft(bs->client);
		}
		else if (!rightBlocked) {
			trap->EA_MoveRight(bs->client);
		}
		else {
			// BALANCED: Only jump when not in combat and with cooldown
			if (!bs->currentEnemy && bs->noJumpTime < level.time && bs->lastObstacleTime < level.time - 2000) {
				trap->EA_Jump(bs->client);
				bs->noJumpTime = level.time + 1000;
				bs->lastObstacleTime = level.time;
			}
			else if (bs->noRetreatTime < level.time) {
				// In combat - back up instead of jumping
				trap->EA_MoveBack(bs->client);
				bs->noRetreatTime = level.time + 500;
			}
		}
		return qtrue; // Path finding handled
	}

	return qfalse; // No path blocking
} // Niksata Edit - BALANCED VERSION

// ================================
// Main Bot Movement
// ================================
// ENHANCED MOVEMENT WITH COMBAT INTEGRATION
// ================================
void NewBotAI_GetMovement(bot_state_t* bs) {
	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client) return;

	int dist = bs->frame_Enemy_Len;

	// Update swing memory FIRST
	Bot_UpdateEnemySwingMemory(bs);

	// Store enemy position for prediction
	VectorCopy(bs->currentEnemy->client->ps.origin, bs->lastEnemyPos);
	bs->lastEnemyTime = level.time;

	// Saber combat clears forced movement
	if (bs->cur_ps.weapon == WP_SABER) {
		Bot_ClearForcedMovement(bs);
	}

	// ================================
	// ENHANCED COMBAT MOVEMENT INTEGRATION
	// ================================

	// If in saber combat, let combat system handle movement
	if (bs->cur_ps.weapon == WP_SABER && dist <= 100) {
		// BALANCED: Only check obstacles when not actively fighting and with cooldown
		if (bs->noUseTime < level.time && bs->lastObstacleTime < level.time - 3000) {
			if (Bot_CheckAndInteractWithObstacle(bs)) {
				bs->lastObstacleTime = level.time;
				return; // Obstacle handling takes priority
			}
		}

		// Combat-specific movement patterns with path validation
		if (dist > 90) {
			vec3_t forward;
			AngleVectors(bs->viewangles, forward, NULL, NULL);

			// Check path before moving
			trace_t tr;
			vec3_t start, end;
			VectorCopy(bs->origin, start);
			start[2] += 24;
			VectorMA(start, 32.0f, forward, end);
			end[2] += 24;

			trap->Trace(&tr, start, NULL, NULL, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

			if (tr.fraction >= 0.8f) {
				trap->EA_MoveForward(bs->client);
			}
			else {
				// Path blocked - try strafing
				if (Q_irand(0, 1)) trap->EA_MoveLeft(bs->client);
				else trap->EA_MoveRight(bs->client);
			}
		}
		else if (dist < 80) {
			// Retreat with obstacle awareness
			vec3_t forward;
			AngleVectors(bs->viewangles, forward, NULL, NULL);

			trace_t tr;
			vec3_t start, end;
			VectorCopy(bs->origin, start);
			start[2] += 24;
			VectorMA(start, -32.0f, forward, end);
			end[2] += 24;

			trap->Trace(&tr, start, NULL, NULL, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

			if (tr.fraction >= 0.8f) {
				trap->EA_MoveBack(bs->client);
			}
			else {
				// Path blocked - try dodging
				Bot_EvadeSwingArc(bs, dist);
			}
		}
		return;
	}

	// ================================
	// BALANCED: OBSTACLE CHECK with cooldown and combat awareness
	// ================================
	if (bs->noUseTime < level.time && bs->lastObstacleTime < level.time - 3000 && (!bs->currentEnemy || dist > 200)) {
		if (Bot_CheckAndInteractWithObstacle(bs)) {
			bs->lastObstacleTime = level.time;
			return; // Obstacle interaction takes priority
		}
	}

	// ENHANCED: Continuous obstacle scanning
	if (Bot_CheckMovementPaths(bs)) {
		return; // Path finding takes priority
	}

	// Direction vectors
	vec3_t toEnemy, forward, right;
	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, toEnemy);
	toEnemy[2] = 0;
	VectorNormalize(toEnemy);
	AngleVectors(bs->viewangles, forward, right, NULL);

	float frontDot = DotProduct(forward, toEnemy);

	// ================================
	// ENHANCED THREAT DETECTION
	// ================================
	qboolean shouldDodge = qfalse;
	int dodgeDir = 0;

	// Check for incoming swing
	if (Bot_ShouldBackstepForSwing(bs)) {
		shouldDodge = qtrue;
		dodgeDir = Bot_GetAdaptiveDodgeDir(bs);
	}

	// ================================
	// PRIORITY: OBSTACLE > DODGE > MOVEMENT
	// ================================
	if (shouldDodge) {
		// Execute dodge with combat awareness
		Bot_EvadeSwingArc(bs, dist);

		// Add adaptive dodge direction
		switch (dodgeDir) {
		case -1: // Dodge left
			trap->EA_MoveLeft(bs->client);
			if (dist > 120) trap->EA_MoveForward(bs->client);
			break;
		case 1:  // Dodge right
			trap->EA_MoveRight(bs->client);
			if (dist > 120) trap->EA_MoveForward(bs->client);
			break;
		case 2:  // Dodge back
			trap->EA_MoveBack(bs->client);
			break;
		}

		// BALANCED: Reduced random jump chance (10% instead of 30%) and only when not in combat
		if (!bs->currentEnemy || dist > 150) {
			if (Q_flrand(0, 1) < 0.1f) {
				if (Q_irand(0, 1)) trap->EA_Jump(bs->client);
				else trap->EA_Jump(bs->client); // Niksata Edit
			}
		}

		return; // Dodge takes priority
	}

	// ================================
	// ENHANCED DYNAMIC COMBAT BAND
	// ================================
	const float IDEAL_MIN = 50.0f;
	const float IDEAL_MAX = 100.0f;

	// Adjust ideal range based on situation
	float adjustedMin = IDEAL_MIN;
	float adjustedMax = IDEAL_MAX;

	// Adjust based on health
	if (g_entities[bs->client].health < 40) {
		adjustedMin += 20; // Stay further back when low health
		adjustedMax += 20;
	}

	// Adjust based on enemy weapon
	if (bs->currentEnemy->client->ps.weapon == WP_SABER) {
		// Enemy has saber - maintain optimal saber range
		adjustedMin = 85;
		adjustedMax = 130;
	}

	// Adjust based on enemy health
	if (bs->currentEnemy->health < 30) {
		adjustedMin -= 10; // Close in on low health enemy
		adjustedMax -= 10;
	}

	// Too close - strong retreat
	if (dist < adjustedMin) {
		// Enhanced retreat with obstacle awareness
		vec3_t forward;
		AngleVectors(bs->viewangles, forward, NULL, NULL);

		trace_t tr;
		vec3_t start, end;
		VectorCopy(bs->origin, start);
		start[2] += 24;
		VectorMA(start, -32.0f, forward, end);
		end[2] += 24;

		trap->Trace(&tr, start, NULL, NULL, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

		if (tr.fraction >= 0.8f) {
			trap->EA_MoveBack(bs->client);
		}
		else {
			// Path blocked - try dodging
			if (!Bot_CheckMovementPaths(bs)) {
				Bot_EvadeSwingArc(bs, dist);
			}
		}
		return;
	}

	// Too far - aggressive close-in
	if (dist > adjustedMax) {
		// Enhanced advance with obstacle awareness
		vec3_t forward;
		AngleVectors(bs->viewangles, forward, NULL, NULL);

		trace_t tr;
		vec3_t start, end;
		VectorCopy(bs->origin, start);
		start[2] += 24;
		VectorMA(start, 32.0f, forward, end);
		end[2] += 24;

		trap->Trace(&tr, start, NULL, NULL, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

		if (tr.fraction >= 0.8f) {
			trap->EA_MoveForward(bs->client);
		}
		else {
			// Path blocked - try flanking
			if (!Bot_CheckMovementPaths(bs)) {
				// Try intelligent flanking
				vec3_t toEnemy;
				VectorSubtract(bs->currentEnemy->client->ps.origin, bs->origin, toEnemy);
				float enemyDot = DotProduct(toEnemy, right);

				if (enemyDot > 0) {
					trap->EA_MoveRight(bs->client);
					trap->EA_MoveForward(bs->client);
				}
				else {
					trap->EA_MoveLeft(bs->client);
					trap->EA_MoveForward(bs->client);
				}
			}
		}
		return;
	}

	// ================================
	// ENHANCED INTELLIGENT STRAFING
	// ================================

	// Maintain strafe burst with memory influence
	if (bs->strafeExpireTime < level.time) {
		float strafeChance = 0.5f;

		// Adjust strafe chance based on situation
		if (bs->currentEnemy->client->ps.weapon == WP_SABER) {
			strafeChance = 0.7f; // More strafing against saber
		}

		if (g_entities[bs->client].health < 50) {
			strafeChance = 0.8f; // More strafing when low health
		}

		if (bs->currentEnemy->health < 40) {
			strafeChance = 0.3f; // Less strafing when enemy is low health
		}

		if (Q_flrand(0, 1) < strafeChance) {
			// ENHANCED: Intelligent strafe direction
			int preferredDir = Bot_GetAdaptiveDodgeDir(bs);

			// Consider enemy position for strafe direction
			vec3_t toEnemy;
			VectorSubtract(bs->currentEnemy->client->ps.origin, bs->origin, toEnemy);
			vectoangles(toEnemy, toEnemy);

			float angleDiff = AngleDifference(bs->viewangles[YAW], toEnemy[YAW]);

			if (angleDiff > 45) {
				// Enemy to right - prefer left strafe
				bs->strafeDir = -1;
			}
			else if (angleDiff < -45) {
				// Enemy to left - prefer right strafe
				bs->strafeDir = 1;
			}
			else {
				// Enemy roughly centered - use adaptive direction
				bs->strafeDir = preferredDir;
			}

			bs->strafeExpireTime = level.time + Q_irand(400, 800);
		}
		else {
			bs->strafeDir = 0;
		}
	}

	// Execute strafe with combat awareness
	if (bs->strafeDir != 0) {
		if (bs->strafeDir == 1) trap->EA_MoveRight(bs->client);
		else trap->EA_MoveLeft(bs->client);

		// Forward bias while attacking
		if (frontDot > 0.3f && dist > 100) {
			trap->EA_MoveForward(bs->client);
		}
	}
	else {
		// Subtle spacing correction with obstacle awareness
		if (dist > 115) {
			// Check forward path
			vec3_t forward;
			AngleVectors(bs->viewangles, forward, NULL, NULL);

			trace_t tr;
			vec3_t start, end;
			VectorCopy(bs->origin, start);
			start[2] += 24;
			VectorMA(start, 16.0f, forward, end);
			end[2] += 24;

			trap->Trace(&tr, start, NULL, NULL, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

			if (tr.fraction >= 0.8f) {
				trap->EA_MoveForward(bs->client);
			}
		}
		else if (dist < 105) {
			// Check backward path
			vec3_t forward;
			AngleVectors(bs->viewangles, forward, NULL, NULL);

			trace_t tr;
			vec3_t start, end;
			VectorCopy(bs->origin, start);
			start[2] += 24;
			VectorMA(start, -16.0f, forward, end);
			end[2] += 24;

			trap->Trace(&tr, start, NULL, NULL, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

			if (tr.fraction >= 0.8f) {
				trap->EA_MoveBack(bs->client);
			}
		}
	}

	// ================================
	// REAR THREAT CHECK
	// ================================
	Bot_CheckRearThreat(bs, frontDot);
} // Niksata Edit - BALANCED VERSION

qboolean BG_InRoll3(int anim)
{
	switch (anim)
	{
	case BOTH_GETUP_BROLL_B:
	case BOTH_GETUP_BROLL_F:
	case BOTH_GETUP_BROLL_L:
	case BOTH_GETUP_BROLL_R:
	case BOTH_GETUP_FROLL_B:
	case BOTH_GETUP_FROLL_F:
	case BOTH_GETUP_FROLL_L:
	case BOTH_GETUP_FROLL_R:
	case BOTH_ROLL_F:
	case BOTH_ROLL_B:
	case BOTH_ROLL_R:
	case BOTH_ROLL_L:
		return qtrue;
	}
	return qfalse;
}

qboolean NewBotAI_IsEnemyPullable(bot_state_t* bs) {
	if (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB))
		return qfalse;
	if (bs->cur_ps.fd.forcePower < 20)
		return qfalse;

	if (BG_InKnockDown(bs->currentEnemy->client->ps.legsAnim))
		return qtrue;
	if (bs->currentEnemy->client->ps.groundEntityNum != ENTITYNUM_NONE - 1)
		return qtrue;
	if (bs->currentEnemy->client->ps.fd.forcePower < 20)
		return qtrue;
	if (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_DRAIN))
		return qtrue;
	if (BG_InRoll3(bs->currentEnemy->client->ps.legsAnim))
		return qtrue;

	return qfalse;
}

int NewBotAI_GetPull(bot_state_t* bs) {
	const int ourHealth = g_entities[bs->client].health, hisHealth = bs->currentEnemy->health, ourForce = bs->cur_ps.fd.forcePower;
	int healthDiff = ourHealth - hisHealth;
	float weight = (float)healthDiff;
	if (g_forcePowerDisable.integer & (1 << FP_PULL))
		return 0;
	if (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_PULL)))
		return 0;
	if (bs->frame_Enemy_Len > 640) //Check pull range..
		return 0;
	if (bs->frame_Enemy_Len < 50)
		return 0; //dont need to pull, we are so close
	if (!bs->frame_Enemy_Vis)
		return 0;
	if (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB))
		return 0;
	if (ourForce < 21)
		return 0;

	if (weight < 1)
		weight = 1;

	if (bs->currentEnemy->client->ps.saberInFlight)
		weight = 0.1f;

	if ((bs->currentEnemy->client->ps.saberMove > 1) && bs->currentEnemy->client->ps.fd.saberAnimLevel != SS_STRONG)
		weight *= 0.2f; //dont pull red vert swings into us unless we its really important

	if (bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT)
		weight *= 0.1f; //Dont cancel a charge unless its important

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_ABSORB)) //less weight if we don't regen fp..
		weight *= 0.5f;
	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_PROTECT))
		weight *= 0.5f;

	if (bs->frame_Enemy_Len < 200 && ourForce >= 20) { //Pulling their weapon should be top priority always
		if (bs->currentEnemy->client->ps.weapon >= WP_BLASTER)
			return 100;
	}

	if (NewBotAI_IsEnemyPullable(bs) && (bs->cur_ps.weapon == WP_SABER || bs->cur_ps.weapon == WP_MELEE) && g_flipKick.integer) {
		if (hisHealth <= 20 && bs->frame_Enemy_Len < 250) {//Check for the insta kill, this should be better maybe... on ground pullablable should be a diff range than in air pullable
			//Com_Printf("pullable 2\n");
			return 100;
		}
		if (BG_InKnockDown(bs->currentEnemy->client->ps.legsAnim)) {
			//Com_Printf("pullable 3\n");
			return (int)(weight * 2);
		}
		//Com_Printf("pullable 1\n");
		if (bs->cur_ps.fd.forceSide == FORCE_LIGHTSIDE) {
			if (bs->frame_Enemy_Len < 250 && ourForce > 32)
				return (int)weight;
		}
		else
			return (int)weight;
	}
	else { //When should we pull stun?
		//Lets say they should be on the same plane roughly..
		float heightDiff = bs->cur_ps.origin[2] - bs->currentEnemy->client->ps.origin[2]; //Us - them.  Positive means we are higher.
		if (heightDiff > -20 && heightDiff < 40) {//If we are less than 20 above or less than 40 below)
			if (bs->frame_Enemy_Len < 100 && ourForce >= 60) { //Close enough and enough force
				weight = (float)ourForce * 0.1f;
				//Com_Printf("weight: %i\n", weight);
				return (int)weight;
			}
		}

	}

	return 0;
}

int NewBotAI_GetPush(bot_state_t* bs) {
	const int ourHealth = g_entities[bs->client].health, ourForce = bs->cur_ps.fd.forcePower;

	if (g_forcePowerDisable.integer & (1 << FP_PUSH))
		return 0;
	if (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_PUSH)))
		return 0;
	if (bs->frame_Enemy_Len > 640)
		return 0;
	if (!bs->frame_Enemy_Vis)
		return 0;
	if (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB))
		return 0;
	if (ourForce < 20)
		return 0;
	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_PROTECT)) //we can tank the dmg..
		return 0;

	if (NewBotAI_IsEnemyPullable(bs) && (ourHealth < 25) && (bs->frame_Enemy_Len < 160) && (bs->currentEnemy->client->ps.weapon == WP_SABER)) {
		if (bs->currentEnemy->client->ps.groundEntityNum == ENTITYNUM_NONE)		//improve this, only if they are coming at us or in air?
			return 100; //Only time we should push atm is to get them off us.. to prevent the flipkick
	}

	return 0;
}

int NewBotAI_GetLightning(bot_state_t* bs) { // Niksata Edit
	const int ourForce = bs->cur_ps.fd.forcePower;
	int weight = 100;

	// Disabled or unknown
	if (g_forcePowerDisable.integer & (1 << FP_LIGHTNING))
		return 0;
	if (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_LIGHTNING)))
		return 0;

	// NEW: Enhanced range checking with tactical considerations
	const int MIN_LIGHTNING_RANGE = 50;  // Minimum range for lightning
	const int MAX_LIGHTNING_RANGE = 1500;  // Maximum range for lightning
	const int OPTIMAL_LIGHTNING_RANGE = 256;  // Optimal range for lightning

	int dist = bs->frame_Enemy_Len;

	// Must be in optimal range and have line of sight
	if (!bs->frame_Enemy_Vis) {
		return 0;  // Can't see enemy
	}

	// Don't use lightning in short range (melee combat)
	if (dist < MIN_LIGHTNING_RANGE) {
		return 0;  // Too close - use saber/other attacks
	}

	if (dist > MAX_LIGHTNING_RANGE) {
		return 0;
	}

	// Bonus for optimal range
	if (dist >= OPTIMAL_LIGHTNING_RANGE - 50 && dist <= OPTIMAL_LIGHTNING_RANGE + 50) {
		weight += 20;  // Optimal range bonus
	}

	// Avoid using lightning if we don't have enough force
	if (ourForce < 30)
		return 0;

	// Additional tactical considerations
	qboolean enemyInMelee = (dist <= 80);
	qboolean enemyLowHealth = (bs->currentEnemy && bs->currentEnemy->health < 40);
	qboolean myHealthLow = (g_entities[bs->client].health < 50);

	// Don't use lightning if enemy is in melee range
	if (enemyInMelee) {
		return 0;  // Use melee combat instead
	}

	// Reduce priority if enemy is very low health and relatively close
	if (enemyLowHealth && dist <= 200) {
		weight -= 40;  // Finish with saber attacks
	}

	// Increase priority if we're low health and enemy is at range
	if (myHealthLow && dist >= MIN_LIGHTNING_RANGE + 50) {
		weight += 30;  // Keep enemy at distance
	}

	// Reduce priority if enemy is already being stunned or knocked down
	if (bs->currentEnemy->client->ps.forceHandExtend == HANDEXTEND_KNOCKDOWN)
		weight -= 50;

	// Slight randomness to avoid spamming
	if (Q_irand(0, 10) < 2)
		weight -= 20;

	return weight;
} // Niksata Edit

int NewBotAI_GetRage(bot_state_t* bs) { // Niksata Edit
	const int ourForce = bs->cur_ps.fd.forcePower;
	const int ourHealth = g_entities[bs->client].health;
	int weight = 2; // Further reduce from 40 to 20

	// Disabled or unknown
	if (g_forcePowerDisable.integer & (1 << FP_RAGE))
		return 0;
	if (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_RAGE)))
		return 0;

	// Must be armed (not unarmed) and enemy very close
	if (bs->cur_ps.weapon == WP_NONE && bs->frame_Enemy_Len < 300)
		return 0;

	// Much stricter force requirement
	if (ourForce < 60) // Increased from <40 to <60
		return 0;

	// Much stricter health conditions
	if (ourHealth < 50) // Changed from <80 to <50
		weight += 15; // Reduced from +20 to +15
	else if (ourHealth < 25) // Changed from <40 to <25
		weight += 20; // Reduced from +30 to +20

	// Reduced bonus for enemy force powers
	if (bs->currentEnemy->client->ps.fd.forcePowersActive & ((1 << FP_PUSH) | (1 << FP_PULL) | (1 << FP_GRIP)))
		weight += 5; // Reduced from +10 to +5

	return weight;
} // Niksata Edit

int NewBotAI_GetDrain(bot_state_t* bs) {
	const int ourHealth = g_entities[bs->client].health, ourForce = bs->cur_ps.fd.forcePower, hisForce = bs->currentEnemy->client->ps.fd.forcePower;
	int weight = 100;

	if (g_forcePowerDisable.integer & (1 << FP_DRAIN))
		return 0;
	if (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_DRAIN)))
		return 0;
	if (bs->frame_Enemy_Len > MAX_DRAIN_DISTANCE)
		return 0;
	if (!bs->frame_Enemy_Vis)
		return 0;
	if (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB))
		return 0;
	if (ourForce < 21)
		return 0;
	if (hisForce == 0)
		return 0;
	if (bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT && bs->cur_ps.weaponChargeTime > 700) //don't drain if we are at a charge
		return 0;

	if (bs->currentEnemy->client->ps.saberInFlight) { //They are saberthrowing
		if (ourHealth > 40) {//We can take the hit
			if (hisForce > 10)
				return (weight - 30);
		}
		else return 0;
	}

	if (ourHealth < 100)
		return ((weight - ourHealth) + 20); //Eeee  //100 - 25 + 20 = 95

	return 0;
}

/*
int NewBotAI_GetWait(bot_state_t *bs) { //Sometimes the best attack is nothing, like when they are trying to saberthrow you and you want to focus on blocking
	int weight = 0;
	if (bs->currentEnemy->client->ps.saberInFlight && bs->frame_Enemy_Len > 200)
		weight = 10;
	return weight;
}
*/

int NewBotAI_GetGrip(bot_state_t* bs) {
	const int ourHealth = g_entities[bs->client].health, hisHealth = bs->currentEnemy->health, ourForce = bs->cur_ps.fd.forcePower, hisForce = bs->currentEnemy->client->ps.fd.forcePower;
	int weight = 100;

	if (g_forcePowerDisable.integer & (1 << FP_GRIP))
		return 0;
	if (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_GRIP)))
		return 0;
	if (bs->frame_Enemy_Len > MAX_GRIP_DISTANCE)
		return 0;
	if (!bs->frame_Enemy_Vis)
		return 0;
	if (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB))
		return 0;
	if (ourForce < 50) //loda fixme
		return 0;

	if (bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT)
		weight *= 0.9f; //Dont cancel a charge unless its important

	if (hisForce < 20) {
		if (((bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_LEVITATION))) || (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_SPEED)) || (bs->currentEnemy->client->saberKnockedTime > level.time)) {
			if (hisHealth < 52)
				return 100;
			return (weight - 10);
		}
	}

	if ((bs->currentEnemy->client->ps.saberMove > 1) && (bs->currentEnemy->client->ps.fd.saberAnimLevel == SS_STRONG && !(bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT)))
		return (ourHealth - hisForce);

	return 0;
}

int NewBotAI_GetTeamEnergize(bot_state_t* bs) {
	int i, weight = 0, force;
	vec3_t diff;

	if (g_forcePowerDisable.integer & (1 << FP_TEAM_FORCE))
		return 0;
	if (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_TEAM_FORCE)))
		return 0;
	if (g_gametype.integer < GT_TEAM)
		return 0;

	g_entities[bs->client].client->ps.fd.forcePowerLevel[FP_TEAM_FORCE] = 3;//hack

	for (i = 0; i < MAX_CLIENTS; i++) {
		if (i == bs->client)
			continue;
		if (!&g_entities[i] || !g_entities[i].client || !g_entities[i].inuse || g_entities[i].health <= 0)
			continue;
		if (g_entities[i].client->ps.fd.forcePower > bs->cur_ps.fd.forcePower)
			continue;
		if (g_entities[i].health > g_entities[bs->client].health)
			continue;

		VectorSubtract(bs->cur_ps.origin, g_entities[i].client->ps.origin, diff);
		if (VectorLengthSquared(diff) > 512 * 512) //out of range
			continue;

		force = 100 - g_entities[i].client->ps.fd.forcePower;

		weight += force * 0.5f; //bots together strong
	}
	return weight;
}

int NewBotAI_GetSaberthrow(bot_state_t* bs) { // Niksata Edit

	//Check if we should saberthrow I guess.
	int dist = bs->frame_Enemy_Len;

	// NEW: Simple range check (300-800 units)
	const int MIN_THROW_RANGE = 500;
	const int MAX_THROW_RANGE = 2500;

	if (bs->cur_ps.weapon == WP_SABER && dist >= MIN_THROW_RANGE && dist <= MAX_THROW_RANGE) {
		if (BG_InKnockDown(bs->currentEnemy->client->ps.legsAnim)) {
			g_entities[bs->client].client->ps.fd.forcePowerLevel[FP_SABERTHROW] = 3;
			g_entities[bs->client].client->ps.fd.forcePowersKnown |= (1 << FP_SABERTHROW);

			if (bs->cur_ps.fd.forcePower > 40 && (bs->currentEnemy->health + bs->currentEnemy->client->ps.stats[STAT_ARMOR]) <= 50) {
				trap->EA_Alt_Attack(bs->client);
				return 100;
			}
			/*
			else if ((((bs->cur_ps.fd.forcePower > bs->currentEnemy->client->ps.fd.forcePower) && bs->cur_ps.fd.forcePower > 40) || (bs->cur_ps.fd.forcePower > 70)) && g_entities[bs->client].health > 75 && bs->currentEnemy->health < 70) {
				trap->EA_Alt_Attack(bs->client);
				Com_Printf("Throwing 2\n");
				return;
			}
			*/
		}
	}
	return 0;
} // Niksata Edit

void NewBotAI_GetDSForcepower(bot_state_t* bs) // Niksata Edit
{
	vec3_t a_fo;
	qboolean useTheForce = qfalse;
	int pushWeight, pullWeight, drainWeight, gripWeight, lighningWeight, rageWeight;
	int minWeight = 0;

	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, a_fo);
	vectoangles(a_fo, a_fo);

	pullWeight = NewBotAI_GetPull(bs);
	pushWeight = NewBotAI_GetPush(bs);
	drainWeight = NewBotAI_GetDrain(bs);
	gripWeight = NewBotAI_GetGrip(bs);
	lighningWeight = NewBotAI_GetLightning(bs);
	rageWeight = NewBotAI_GetRage(bs); // No penalty - normal chance

	// Prefer saber attacks over PUSH/PULL/RAGE when in saber range
	// (do NOT early-return; allow other logic like Grip/Drain/Lightning/Team Energize)
	if (bs->cur_ps.weapon == WP_SABER &&
		bs->currentEnemy &&
		bs->currentEnemy->client &&
		bs->frame_Enemy_Vis &&
		bs->frame_Enemy_Len <= SABER_ATTACK_RANGE)
	{
		pushWeight = 0;
		pullWeight = 0;
		rageWeight = 0;
	}

	if (pushWeight > pullWeight && pushWeight > lighningWeight && pushWeight > gripWeight && pushWeight > drainWeight && pushWeight > rageWeight && pushWeight > minWeight) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_PUSH;
		useTheForce = qtrue;
	}
	else if (pullWeight > pushWeight && pullWeight > lighningWeight && pullWeight > gripWeight && pullWeight > drainWeight && pullWeight > rageWeight && pullWeight > minWeight) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_PULL;
		useTheForce = qtrue;
	}
	else if (lighningWeight > pushWeight && lighningWeight > pullWeight && lighningWeight > gripWeight && lighningWeight > drainWeight && lighningWeight > rageWeight && lighningWeight > minWeight) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_LIGHTNING;
		useTheForce = qtrue;
	}
	else if (gripWeight > pushWeight && gripWeight > pullWeight && gripWeight > lighningWeight && gripWeight > drainWeight && gripWeight > rageWeight && gripWeight > minWeight) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_GRIP;
		useTheForce = qtrue;
	}
	else if (drainWeight > pushWeight && drainWeight > pullWeight && drainWeight > lighningWeight && drainWeight > gripWeight && drainWeight > rageWeight && drainWeight > minWeight) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_DRAIN;
		useTheForce = qtrue;
	}
	else if (rageWeight > pushWeight && rageWeight > pullWeight && rageWeight > lighningWeight && rageWeight > gripWeight && rageWeight > drainWeight && rageWeight > minWeight) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_RAGE;
		useTheForce = qtrue;
	}

	if (!useTheForce && !(g_forcePowerDisable.integer & (1 << FP_SPEED)) && (bs->cur_ps.fd.forcePowersKnown & (1 << FP_SPEED)) && (bs->frame_Enemy_Len > 90)) {
		if (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB)) {
			if (g_entities[bs->client].health <= 100 && (bs->cur_ps.fd.forcePower >= 50)) {
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_SPEED;
				useTheForce = qtrue;
			}
		}
	}

	if (NewBotAI_GetTeamEnergize(bs) > minWeight) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_TEAM_FORCE;
		useTheForce = qtrue;
	}

	if (NewBotAI_GetSaberthrow(bs) > minWeight) {
		int dist = bs->frame_Enemy_Len;

		if (dist >= 500 && dist <= 900) {
			qboolean enemyVisible = bs->frame_Enemy_Vis;
			qboolean enemyKnocked = BG_InKnockDown(bs->currentEnemy->client->ps.legsAnim);

			if (enemyVisible && enemyKnocked) {
				trap->EA_Alt_Attack(bs->client);
			}
		}
	}

	if (useTheForce && (level.framenum % 2) && (!bs->currentEnemy->client->invulnerableTimer || (bs->currentEnemy->client->invulnerableTimer <= level.time)))
		trap->EA_ForcePower(bs->client);
} // Niksata Edit

int NewBotAI_GetTelepathy(bot_state_t* bs) { // Niksata Edit
	const int ourForce = bs->cur_ps.fd.forcePower;
	const int ourHealth = g_entities[bs->client].health;
	const float dist = bs->frame_Enemy_Len;
	int weight = 0;

	// Check if the power is available
	if (g_forcePowerDisable.integer & (1 << FP_TELEPATHY))
		return 0;
	if (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_TELEPATHY)))
		return 0;
	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_TELEPATHY))
		return 0;

	// Require line-of-sight to the enemy to use effectively
	if (!bs->frame_Enemy_Vis)
		return 0;

	// Only use at medium-to-close range; no need far away
	if (dist > 400)
		return 0;

	// Use when near full health to maximize strategic effect
	if (ourHealth >= 90) {
		weight += 50;   // high priority at max health
	}
	else if (ourHealth >= 70) {
		weight += 25;   // moderate priority
	}
	else {
		return 0;       // avoid using if health is low
	}

	// Slight bonus if closer to enemy (for flanking or ambush)
	if (dist < 150) weight += 20;
	else if (dist < 250) weight += 10;

	// Only attempt if enough force is available
	if (ourForce < 20)
		return 0;

	// Add a tiny random factor to prevent predictability
	weight += Q_irand(0, 10);

	return weight;
} // Niksata Edit

int NewBotAI_GetHeal(bot_state_t* bs) {
	const int ourForce = bs->cur_ps.fd.forcePower;
	const int ourHealth = g_entities[bs->client].health;
	int diff = ((ourForce - ourHealth) + 101) * 0.5f; //Range is 0-100?

	if (g_forcePowerDisable.integer & (1 << FP_HEAL))
		return 0;
	if (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_HEAL)))
		return 0;
	//Higher diff is, higher weight to heal?

	if (ourHealth >= 100)
		return 0;
	if (ourForce < 50)
		return 0;
	return diff;
}

int NewBotAI_GetTeamHeal(bot_state_t* bs) {
	int i, weight = 0, health;
	vec3_t diff;

	if (g_forcePowerDisable.integer & (1 << FP_TEAM_HEAL))
		return 0;
	if (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_TEAM_HEAL)))
		return 0;
	if (g_gametype.integer < GT_TEAM)
		return 0;

	for (i = 0; i < MAX_CLIENTS; i++) {
		if (i == bs->client)
			continue;
		if (!&g_entities[i] || !g_entities[i].client || !g_entities[i].inuse || g_entities[i].health <= 0)
			continue;
		if (g_entities[i].client->sess.sessionTeam != g_entities[bs->client].client->sess.sessionTeam)
			continue;
		if (g_entities[i].client->ps.fd.forcePower > bs->cur_ps.fd.forcePower)
			continue;
		if (g_entities[i].health > g_entities[bs->client].health)
			continue;

		VectorSubtract(bs->cur_ps.origin, g_entities[i].client->ps.origin, diff);
		if (VectorLengthSquared(diff) > 512 * 512) //out of range
			continue;

		health = g_entities[i].health;
		if (health > 100 || health <= 0)
			health = 100;
		weight += health * 0.5f; //bots together strong
	}
	return weight;
}

void NewBotAI_GetLSForcepower(bot_state_t* bs) // Niksata Edit
{
	vec3_t a_fo;
	qboolean useTheForce = qfalse;
	int pullWeight, pushWeight, absorbWeight, protectWeight, healWeight, telepathyWeight; // Niksata Edit
	int minWeight = 0;

	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, a_fo);
	vectoangles(a_fo, a_fo);

	pullWeight = NewBotAI_GetPull(bs);
	pushWeight = NewBotAI_GetPush(bs);
	absorbWeight = NewBotAI_GetAbsorb(bs); //why doesn't he absorb when he should
	protectWeight = NewBotAI_GetProtect(bs);
	healWeight = NewBotAI_GetHeal(bs);
	telepathyWeight = NewBotAI_GetTelepathy(bs); // Niksata Edit
	//get weights

	// Prefer saber attacks over OFFENSIVE force powers when in saber range
	// (do NOT block defensive powers like ABSORB/PROTECT/HEAL)
	if (bs->cur_ps.weapon == WP_SABER &&
		bs->currentEnemy &&
		bs->currentEnemy->client &&
		bs->frame_Enemy_Vis &&
		bs->frame_Enemy_Len <= SABER_ATTACK_RANGE)
	{
		pushWeight = 0;
		pullWeight = 0;
		telepathyWeight = 0;
	}

	if (pushWeight > pullWeight && pushWeight > absorbWeight && pushWeight > protectWeight && pushWeight > healWeight && pushWeight > minWeight) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_PUSH;
		useTheForce = qtrue;
		//trap->Print("Push - Weights -- Pull: %i, Push: %i, Absorb: %i, Protect: %i, Heal %i\n", pullWeight, pushWeight, absorbWeight, protectWeight, healWeight);
	}
	else if (pullWeight > pushWeight && pullWeight > absorbWeight && pullWeight > protectWeight && pullWeight > healWeight && pullWeight > minWeight) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_PULL;
		useTheForce = qtrue;
		//trap->Print("Pull - Weights -- Pull: %i, Push: %i, Absorb: %i, Protect: %i, Heal %i\n", pullWeight, pushWeight, absorbWeight, protectWeight, healWeight);
	}
	else if (absorbWeight > pushWeight && absorbWeight > pullWeight && absorbWeight > protectWeight && absorbWeight > healWeight && absorbWeight > minWeight) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_ABSORB;
		useTheForce = qtrue;
		//trap->Print("Absorb - Weights -- Pull: %i, Push: %i, Absorb: %i, Protect: %i, Heal %i\n", pullWeight, pushWeight, absorbWeight, protectWeight, healWeight);
	}
	else if (protectWeight > pushWeight && protectWeight > pullWeight && protectWeight > absorbWeight && protectWeight > healWeight && protectWeight > minWeight) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_PROTECT;
		useTheForce = qtrue;
		//trap->Print("Protect - Weights -- Pull: %i, Push: %i, Absorb: %i, Protect: %i, Heal %i\n", pullWeight, pushWeight, absorbWeight, protectWeight, healWeight);
	}
	else if (telepathyWeight > pushWeight && telepathyWeight > pullWeight && telepathyWeight > absorbWeight && telepathyWeight > healWeight && telepathyWeight > minWeight) { // Niksata Edit
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_TELEPATHY;
		useTheForce = qtrue;
	} // Niksata Edit
	else if (healWeight > protectWeight && healWeight > pushWeight && healWeight > pullWeight && healWeight > absorbWeight && healWeight > minWeight) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_HEAL;
		useTheForce = qtrue;
	}

	if (!useTheForce && !(g_forcePowerDisable.integer & (1 << FP_SPEED)) && (bs->cur_ps.fd.forcePowersKnown & (1 << FP_SPEED)) && (bs->frame_Enemy_Len > 90)) { // Niksata Edit
		if (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB)) {
			if (g_entities[bs->client].health <= 100 && (bs->cur_ps.fd.forcePower >= 50)) {
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_SPEED;
				useTheForce = qtrue;
			}
		}
	} // Niksata Edit
	//Speed, team heal,

	if (!useTheForce && NewBotAI_GetTeamHeal(bs) > minWeight) { //make this use the weight
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_TEAM_HEAL; //ideally loop through in range people and see if they need it
		useTheForce = qtrue;
	}

	//Check if we should saberthrow I guess. // Niksata Edit
	if (NewBotAI_GetSaberthrow(bs) > minWeight) {
		// NEW: Range validation (500-900 units)
		int dist = bs->frame_Enemy_Len;

		if (dist >= 500 && dist <= 2500) {
			// Additional tactical checks
			qboolean enemyVisible = bs->frame_Enemy_Vis;
			qboolean enemyKnocked = BG_InKnockDown(bs->currentEnemy->client->ps.legsAnim);

			if (enemyVisible && enemyKnocked) {
				trap->EA_Alt_Attack(bs->client);
			}
		}
	} // Niksata Edit

	//if (bs->cur_ps.weaponstate != WEAPON_CHARGING_ALT && (level.clients[bs->client].ps.fd.forcePowerSelected == FP_PULL) && random() > 0.5)
		//useTheForce = qfalse;

	if (useTheForce && (level.framenum % 2) && (!bs->currentEnemy->client->invulnerableTimer || (bs->currentEnemy->client->invulnerableTimer <= level.time))) {
		trap->EA_ForcePower(bs->client);
		//Com_Printf("Using force\n");
	}
} // Niksata Edit

void NewBotAI_DSvDS(bot_state_t* bs) // Niksata Edit
{
	// NEW: Check FP level before using force powers
	if (bs->cur_ps.fd.forcePower < 10) {
		// Don't use force powers when FP < 10 - skip force power logic
		NewBotAI_GetAim(bs); //If a saber is the closest entity and it is in flight, aim at it?

		if (bs->cur_ps.forceHandExtend == HANDEXTEND_KNOCKDOWN) {
			NewBotAI_Getup(bs);
			return;
		}

		if (bs->cur_ps.fd.forceGripBeingGripped > level.time) {//We are being gripped //bs->cur_ps.fd.forceGripCripple
			NewBotAI_ReactToBeingGripped(bs);
			return;
		}

		// Skip all force power active checks when FP < 10

		if (bs->cur_ps.saberInFlight) { // Niksata Edit
			int dist = bs->frame_Enemy_Len;

			// Define optimal saber throw ranges
			const int MIN_THROW_RANGE = 500;  // Minimum range for effective throw
			const int MAX_THROW_RANGE = 2500;  // Maximum effective range
			const int MELEE_RANGE = 64;       // Melee combat range

			// Check if enemy is in optimal range for saber throw
			if (dist >= MIN_THROW_RANGE && dist <= MAX_THROW_RANGE) {
				// Good range for saber throw
				NewBotAI_SaberThrowing(bs);
			}
			else if (dist < MELEE_RANGE) {
				// Enemy is too close - use melee combat
				// Don't throw saber, let regular combat handle it
				return;
			}
			else if (dist < MIN_THROW_RANGE) {
				// Too close for effective throw but not in melee
				// Use regular saber combat instead
				return;
			}
			else if (dist > MAX_THROW_RANGE) {
				// Too far for effective throw
				// Move closer instead of throwing
				return;
			}
		} // Niksata Edit

		NewBotAI_GetMovement(bs);
		// Skip force power selection when FP < 10
		NewBotAI_GetAttack(bs);
		return; // Exit early when FP < 10
	}

	// Normal force power logic when FP >= 10
	NewBotAI_GetAim(bs); //If a saber is the closest entity and it is in flight, aim at it?

	if (bs->cur_ps.forceHandExtend == HANDEXTEND_KNOCKDOWN) {
		NewBotAI_Getup(bs);
		return;
	}

	if (bs->cur_ps.fd.forceGripBeingGripped > level.time) {//We are being gripped //bs->cur_ps.fd.forceGripCripple
		NewBotAI_ReactToBeingGripped(bs);
		return;
	}

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_LIGHTNING)) { // Niksata Edit
		NewBotAI_Lightning(bs);
		return;
	} // Niksata Edit

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_RAGE)) { // Niksata Edit
		NewBotAI_Raging(bs);
		return;
	} // Niksata Edit

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_GRIP)) {
		NewBotAI_Gripkick(bs);
		return;
	}

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_DRAIN)) {
		NewBotAI_Draining(bs);
		return;
	}

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_SPEED)) {
		NewBotAI_Speeding(bs);
	}

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_RAGE)) {
		NewBotAI_Raging(bs);
	}

	if (bs->cur_ps.saberInFlight) { // Niksata Edit
		int dist = bs->frame_Enemy_Len;

		// Define optimal saber throw ranges
		const int MIN_THROW_RANGE = 500;  // Minimum range for effective throw
		const int MAX_THROW_RANGE = 2500;  // Maximum effective range
		const int MELEE_RANGE = 64;       // Melee combat range

		// Check if enemy is in optimal range for saber throw
		if (dist >= MIN_THROW_RANGE && dist <= MAX_THROW_RANGE) {
			// Good range for saber throw
			NewBotAI_SaberThrowing(bs);
		}
		else if (dist < MELEE_RANGE) {
			// Enemy is too close - use melee combat
			// Don't throw saber, let regular combat handle it
			return;
		}
		else if (dist < MIN_THROW_RANGE) {
			// Too close for effective throw but not in melee
			// Use regular saber combat instead
			return;
		}
		else if (dist > MAX_THROW_RANGE) {
			// Too far for effective throw
			// Move closer instead of throwing
			return;
		}
	} // Niksata Edit

	NewBotAI_GetMovement(bs);
	if (g_forcePowerDisable.integer != 163837 && g_forcePowerDisable.integer != 163839)
		NewBotAI_GetDSForcepower(bs);
	NewBotAI_GetAttack(bs);
} // Niksata Edit

void NewBotAI_DSvLS(bot_state_t* bs) // Niksata Edit
{
	// NEW: Check FP level before using force powers
	if (bs->cur_ps.fd.forcePower < 10) {
		// Don't use force powers when FP < 10 - skip force power logic
		NewBotAI_GetAim(bs);

		if (bs->cur_ps.forceHandExtend == HANDEXTEND_KNOCKDOWN) {
			NewBotAI_Getup(bs);
			return;
		}

		// Skip all force power active checks when FP < 10

		NewBotAI_GetMovement(bs);
		// Skip force power selection when FP < 10
		NewBotAI_GetAttack(bs);
		return; // Exit early when FP < 10
	}

	// Normal force power logic when FP >= 10
	NewBotAI_GetAim(bs);

	if (bs->cur_ps.forceHandExtend == HANDEXTEND_KNOCKDOWN) {
		NewBotAI_Getup(bs);
		return;
	}

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_LIGHTNING)) { // Niksata Edit
		NewBotAI_Lightning(bs);
		return;
	} // Niksata Edit

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_RAGE)) { // Niksata Edit
		NewBotAI_Raging(bs);
		return;
	} // Niksata Edit

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_GRIP)) {
		NewBotAI_Gripkick(bs);
		return;
	}

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_DRAIN)) {
		NewBotAI_Draining(bs);//y return? y not getmovement?
		return;
	}

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_SPEED)) {
		NewBotAI_Speeding(bs);
	}

	NewBotAI_GetMovement(bs);
	if (g_forcePowerDisable.integer != 163837 && g_forcePowerDisable.integer != 163839)
		NewBotAI_GetDSForcepower(bs);
	NewBotAI_GetAttack(bs);
} // Niksata Edit

void NewBotAI_LSvDS(bot_state_t* bs) // Niksata Edit
{
	// NEW: Check FP level before using force powers
	if (bs->cur_ps.fd.forcePower < 10) {
		// Don't use force powers when FP < 10 - skip force power logic
		NewBotAI_GetAim(bs);

		if (bs->cur_ps.forceHandExtend == HANDEXTEND_KNOCKDOWN) {
			NewBotAI_Getup(bs);
			return;
		}
		if (bs->cur_ps.fd.forceGripBeingGripped > level.time) {
			NewBotAI_ReactToBeingGripped(bs);
			return;
		}

		// Skip all force power active checks when FP < 10

		if (bs->cur_ps.saberInFlight) {
			NewBotAI_SaberThrowing(bs);
		}

		NewBotAI_GetMovement(bs);
		// Skip force power selection when FP < 10
		NewBotAI_GetAttack(bs);
		return; // Exit early when FP < 10
	}

	// Normal force power logic when FP >= 10
	NewBotAI_GetAim(bs);

	if (bs->cur_ps.forceHandExtend == HANDEXTEND_KNOCKDOWN) {
		NewBotAI_Getup(bs);
		return;
	}
	if (bs->cur_ps.fd.forceGripBeingGripped > level.time) {
		NewBotAI_ReactToBeingGripped(bs);
		return;
	}
	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_SPEED))
		NewBotAI_Speeding(bs);
	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_HEAL)) // Niksata Edit
		NewBotAI_Healing(bs); // Niksata Edit
	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_TELEPATHY)) // Niksata Edit
		NewBotAI_MindTrick(bs); // Niksata Edit
	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_PROTECT))
		NewBotAI_Protecting(bs);
	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_ABSORB))
		NewBotAI_Absorbing(bs);
	if (bs->cur_ps.saberInFlight) {
		NewBotAI_SaberThrowing(bs);
	}

	NewBotAI_GetMovement(bs);
	if (g_forcePowerDisable.integer != 163837 && g_forcePowerDisable.integer != 163839)
		NewBotAI_GetLSForcepower(bs);
	NewBotAI_GetAttack(bs);
} // Niksata Edit

void NewBotAI_LSvLS(bot_state_t* bs) // Niksata Edit
{
	// NEW: Check FP level before using force powers
	if (bs->cur_ps.fd.forcePower < 10) {
		// Don't use force powers when FP < 10 - skip force power logic
		NewBotAI_GetAim(bs);

		if (bs->cur_ps.forceHandExtend == HANDEXTEND_KNOCKDOWN) {
			NewBotAI_Getup(bs);
			return;
		}

		// Skip all force power active checks when FP < 10

		NewBotAI_GetMovement(bs);
		// Skip force power selection when FP < 10
		NewBotAI_GetAttack(bs);
		return; // Exit early when FP < 10
	}

	// Normal force power logic when FP >= 10
	NewBotAI_GetAim(bs);

	if (bs->cur_ps.forceHandExtend == HANDEXTEND_KNOCKDOWN) {
		NewBotAI_Getup(bs);
		return;
	}

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_SPEED))
		NewBotAI_Speeding(bs);
	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_PROTECT))
		NewBotAI_Protecting(bs);
	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_ABSORB))
		NewBotAI_Absorbing(bs);

	NewBotAI_GetMovement(bs);
	if (g_forcePowerDisable.integer != 163837 && g_forcePowerDisable.integer != 163839)
		NewBotAI_GetLSForcepower(bs);
	NewBotAI_GetAttack(bs);
} // Niksata Edit

void NewBotAI_NF(bot_state_t* bs)
{
	// qboolean swing = qfalse;
	const float speed = NewBotAI_GetSpeedTowardsEnemy(bs);

	NewBotAI_GetAim(bs);

	if (bs->cur_ps.forceHandExtend == HANDEXTEND_KNOCKDOWN) {
		NewBotAI_Getup(bs);
		return;
	}

	g_entities[bs->client].client->ps.fd.saberAnimLevel = SS_STRONG;
	//if ((g_entities[bs->client].client->ps.saberMove == LS_A_L2R) || (g_entities[bs->client].client->ps.saberMove == LS_A_TL2BR) || (g_entities[bs->client].client->ps.saberMove == LS_R_L2R))
		//horiz = qtrue;

	/*
	if (g_entities[bs->client].client->ps.saberMove > 1)
		swing = qtrue;
	*/

	if (bs->frame_Enemy_Len >= 325) {
		trap->EA_MoveForward(bs->client);
	}
	else if (bs->frame_Enemy_Len <= 200) {//Closerange
		if (g_entities[bs->client].client->ps.saberMove > 1) {
			if (bs->origin[2] < bs->currentEnemy->client->ps.origin[2]) {
				trap->EA_Jump(bs->client);
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_LEVITATION;
				trap_EA_ForcePower(bs->client);
			}
			else if (g_entities[bs->client].client->ps.groundEntityNum == ENTITYNUM_NONE) {
				trap->EA_Crouch(bs->client);
			}
			else if (bs->currentEnemy->client->ps.legsAnim == BOTH_CROUCH1IDLE || bs->currentEnemy->client->ps.legsAnim == BOTH_CROUCH1WALK ||
				bs->currentEnemy->client->ps.legsAnim == BOTH_CROUCH1WALKBACK || bs->currentEnemy->client->ps.legsAnim == BOTH_MEDITATE) {
				trap->EA_Crouch(bs->client);
			}
			trap->EA_MoveForward(bs->client);
		}
		else if ((g_tweakSaber.integer & ST_EASYBACKSLASH) && (bs->frame_Enemy_Len < 128) && (g_backslashDamageScale.value >= 5 || (g_backslashDamageScale.value >= 3 && (g_tweakSaber.integer & ST_SPINBACKSLASH)))) {
			//Do a backslash 
			//bs->ideal_viewangles[YAW] += 180;
			/*if (BS_GroundDistance(bs) < 20)
				trap->EA_Jump(bs->client);
			else
				trap->EA_Crouch(bs->client);*/ // Niksata Edit
			trap->EA_MoveBack(bs->client);
			if (g_entities[bs->client].client->ps.legsAnim != BOTH_ROLL_F)
				trap->EA_Attack(bs->client);
		}
		else {
			trap->EA_MoveRight(bs->client);
			if (bs->currentEnemy->client->ps.saberMove > 1) {

				if (g_entities[bs->client].client->ps.saberMove >= LS_PARRY_UP && g_entities[bs->client].client->ps.saberMove <= LS_PARRY_LL)
					trap_EA_Attack(bs->client);
				else {
					trap_EA_MoveBack(bs->client);
					trap_EA_Jump(bs->client);
				}
			}
			else
				if (g_entities[bs->client].client->ps.legsAnim != BOTH_ROLL_F)
					trap->EA_Attack(bs->client);
		}
	}
	else if ((speed >= 125) && ((bs->frame_Enemy_Len / speed) > 0.7f)) {//Midrange
		if ((sqrt(bs->cur_ps.velocity[0] * bs->cur_ps.velocity[0] + bs->cur_ps.velocity[1] * bs->cur_ps.velocity[1])) > 240.0f) {
			trap->EA_Jump(bs->client);
			if (BS_GroundDistance(bs) > 20 && g_entities[bs->client].client->ps.saberMove <= 1) {
				trap->EA_Crouch(bs->client);
				trap->EA_MoveBack(bs->client);
				trap->EA_MoveRight(bs->client);
				trap->EA_Attack(bs->client);
			}
		}
		trap->EA_MoveForward(bs->client);
	}
	else {
		trap->EA_MoveForward(bs->client);
	}

#if 0
	if (swing && bs->frame_Enemy_Len < 200 && g_entities[bs->client].client->ps.saberMove != 13) //fuck trying to aim backslash like this
	{
		vec3_t saberEnd, saberAngs;
		VectorCopy(g_entities[bs->client].client->saber[0].blade[0].trail.tip, saberEnd); //Vector of the tip of the saber 
		VectorSubtract(saberEnd, bs->origin, saberEnd);//This might be backwards, but its the vector of saber tip relative to us

		vectoangles(saberEnd, saberAngs); //Turn saber tip into angles

		saberAngs[YAW] -= 150;

		saberAngs[PITCH] += 25;//who knows!

		saberAngs[YAW] = AngleSubtract(saberAngs[YAW], bs->viewangles[YAW]);
		saberAngs[PITCH] = AngleSubtract(saberAngs[PITCH], bs->viewangles[PITCH]);

		bs->ideal_viewangles[YAW] -= saberAngs[YAW]; //Offset our ideal angles by this to keep saber tip always pointed at enemy?
		bs->ideal_viewangles[PITCH] -= saberAngs[PITCH] * 0.5;

		/*
		//Poke time!
		if (level.time - bs->chickenWussCalculationTime > 500) {//i hope this isnt being used for anything else
			bs->chickenWussCalculationTime = level.time;
			if (bs->aimOffsetAmtYaw > 0) {
				bs->aimOffsetAmtYaw = -20;
			}
			else {
				bs->aimOffsetAmtYaw = 20;
			}
		}
		*/

		//bs->ideal_viewangles[YAW] += bs->aimOffsetAmtYaw;


		//if (swing)
			//bs->ideal_viewangles[PITCH] += (Q_flrand(-1.0f, 1.0f) * 12);
	}
#endif

	//1 - Get moves and movement
	//2 - Get aim (offset?) //self->client->saber[saberNum].blade[bladeNum].trail.tip
	//3 - get poke offset6
	//Run for hp if low?

	//NewBotAI_GetNFActions(bs);
	//NewBotAI_GetMovement(bs);
	//NewBotAI_GetAttack(bs);
}

void G_Kill(gentity_t* ent);
qboolean NewBotAI_CapRoute(bot_state_t* bs, float thinktime)
{
	int activeCapRoute, activeCapRouteSequence; //sequence,
	vec3_t newSpot = { 0 };

	if (level.gametype != GT_CTF || !g_entities[bs->client].client || !g_entities[bs->client].client->pers.activeCapRoute)
		return qfalse;

	if (level.clients[bs->client].sess.sessionTeam == TEAM_RED) {
		activeCapRoute = g_entities[bs->client].client->pers.activeCapRoute;
		activeCapRouteSequence = g_entities[bs->client].client->activeCapRouteSequence;
		//Com_Printf("Seq %i max %i\n", activeCapRouteSequence, redRouteList[g_entities[bs->client].client->activeCapRoute].length);
		if (activeCapRouteSequence >= redRouteList[g_entities[bs->client].client->pers.activeCapRoute - 1].length) {
			g_entities[bs->client].client->pers.activeCapRoute = 0;
			G_Kill(&g_entities[bs->client]);
			return qfalse;//route over.  self kill?
		}
		newSpot[0] = redRouteList[activeCapRoute - 1].pos[activeCapRouteSequence][0];
		newSpot[1] = redRouteList[activeCapRoute - 1].pos[activeCapRouteSequence][1];
		newSpot[2] = redRouteList[activeCapRoute - 1].pos[activeCapRouteSequence][2];
		g_entities[bs->client].client->activeCapRouteSequence++;
	}
	else if (level.clients[bs->client].sess.sessionTeam == TEAM_BLUE) {
		activeCapRoute = g_entities[bs->client].client->pers.activeCapRoute;
		activeCapRouteSequence = g_entities[bs->client].client->activeCapRouteSequence;
		//Com_Printf("Seq %i max %i\n", activeCapRouteSequence, blueRouteList[g_entities[bs->client].client->activeCapRoute].length);
		if (activeCapRouteSequence >= blueRouteList[g_entities[bs->client].client->pers.activeCapRoute - 1].length) {
			g_entities[bs->client].client->pers.activeCapRoute = 0;
			G_Kill(&g_entities[bs->client]);
			return qfalse;//route over.  self kill?
		}

		//Com_Printf("^5Setting origin for route %i seq %i\n", activeCapRoute, activeCapRouteSequence);
		newSpot[0] = blueRouteList[activeCapRoute - 1].pos[activeCapRouteSequence][0];
		newSpot[1] = blueRouteList[activeCapRoute - 1].pos[activeCapRouteSequence][1];
		newSpot[2] = blueRouteList[activeCapRoute - 1].pos[activeCapRouteSequence][2];
		g_entities[bs->client].client->activeCapRouteSequence++;
	}
	else {
		return qfalse;
	}

	trap->EA_Action(bs->client, ACTION_SKI);
	bs->ideal_viewangles[YAW] = vectoyaw(g_entities[bs->client].client->ps.velocity);

	if (g_entities[bs->client].client->ps.velocity[2]) {
		if (level.time % 1000 > 500) { //what the fuck /sad hack to make it lok like they are jetting
			trap->EA_Jump(bs->client);
			trap->EA_MoveUp(bs->client);
		}
		else {
			trap->EA_MoveDown(bs->client);
			trap->EA_Crouch(bs->client);
		}
	}

	{
		trace_t tr;
		vec3_t playerMins = { -15, -15, DEFAULT_MINS_2 };
		vec3_t playerMaxs = { 15, 15, DEFAULT_MAXS_2 };

		JP_Trace(&tr, g_entities[bs->client].client->ps.origin, playerMins, playerMaxs, newSpot, bs->client, CONTENTS_BODY, qfalse, 0, 0);

		if (tr.fraction != 1) { //Hit someone else
			g_entities[bs->client].client->pers.activeCapRoute = 0;
			return qfalse;
		}

		VectorSubtract(newSpot, g_entities[bs->client].client->ps.origin, g_entities[bs->client].client->ps.velocity);
		VectorScale(g_entities[bs->client].client->ps.velocity, 40.0f, g_entities[bs->client].client->ps.velocity);//sv_fps ?
		//Com_Printf("Vel is %.1f\n", len);
		//VectorClear(g_entities[bs->client].client->ps.velocity);

		//VectorCopy(newSpot, g_entities[bs->client].client->ps.origin);
	}

	return qtrue;
}

void NewBotAI_Tribes(bot_state_t* bs, float thinktime)
{
	//For capper bot. need to keep track of what spot we are at.
	//How do we we store which route the bot is currently on?
	//Bot->capRouteSequence starts at 0
	//Set origin to the selected route[sequence]
	//increment sequence

	//Make bot abandon route if knockedback and have him fight instead?
	if (bs->cur_ps.eFlags & EF_JETPACK_FLAMING || bs->cur_ps.eFlags & EF_JETPACK_ACTIVE) {
		//if (!(bs->cur_ps.pm_flags & PMF_JUMP_HELD))
		//{
			//bs->jumpTime = level.time + 200;
			//bs->jumpHoldTime = level.time + 200;
		//}
		//Com_Printf("2 Still going up\n");
		trap->EA_Jump(bs->client);
	}
	else if (bs->cur_ps.fd.forcePower > 98) {
		//Com_Printf("1 Going up\n");
		//bs->jumpTime = level.time + 200;
		//bs->jumpHoldTime = level.time + 200;

		trap->EA_Jump(bs->client);
	}
	else {
		//Com_Printf("3 Flaming? %i, Active? %i, FP: %i\n", (bs->cur_ps.eFlags & EF_JETPACK_FLAMING), bs->cur_ps.eFlags & EF_JETPACK_ACTIVE, bs->cur_ps.fd.forcePower);
	}

	/*
	if (bs->jumpTime > level.time && bs->jDelay < level.time)
	{
	if (bs->jumpHoldTime > level.time)
	{
	trap->EA_Jump(bs->client);
	if (bs->wpCurrent)
	{
	if ((bs->wpCurrent->origin[2] - bs->origin[2]) < 64)
	{
	trap->EA_MoveForward(bs->client);
	}
	}
	else
	{
	trap->EA_MoveForward(bs->client);
	}
	if (g_entities[bs->client].client->ps.groundEntityNum == ENTITYNUM_NONE)
	{
	g_entities[bs->client].client->ps.pm_flags |= PMF_JUMP_HELD;
	}
	}
	else if (!(bs->cur_ps.pm_flags & PMF_JUMP_HELD))
	{
	trap->EA_Jump(bs->client);
	}
		*/
	trap->EA_Action(bs->client, ACTION_SKI);

	StandardBotAI(bs, thinktime);
	NewBotAI_GetAim(bs);
	NewBotAI_GetAttack(bs);
	//NewBotAI_GetMovement(bs);
	//NewBotAI_GetAttack(bs);
}

void NewBotAI_StrafeJump(bot_state_t* bs, float distance)
{
	qboolean aimright = qfalse;
	//	float xyspeed, optimalAngle, frametime = 0.05f, baseSpeed = g_speed.integer;

	NewBotAI_GetAim(bs);

	NewBotAI_Flipkick(bs);

	if (level.time % 1000 > 500) //Switch sides twice a second
		aimright = qtrue;

	/*
	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_SPEED))
		baseSpeed *= 1.7f;

	xyspeed = sqrt(bs->cur_ps.velocity[0] * bs->cur_ps.velocity[0] + bs->cur_ps.velocity[1] * bs->cur_ps.velocity[1]);

	optimalAngle = acos((double) ((baseSpeed - (baseSpeed * frametime)) / xyspeed)) * (180.0f/M_PI) - 45.0f;
	*/

	trap->EA_MoveForward(bs->client);

	if (aimright) {
		//bs->ideal_viewangles[YAW] += optimalAngle;
		trap->EA_MoveRight(bs->client);
	}
	else {
		//bs->ideal_viewangles[YAW] -= optimalAngle;
		trap->EA_MoveLeft(bs->client);
	}
}

void NewBotAI_DoAloneStuff(bot_state_t* bs, float thinktime) {
	qboolean useTheForce = qfalse;
	int numEnts, i, radiusEnts[256];
	vec3_t mins = { -1024, -1024, -256 }, maxs = { 1024, 1024, 256 }, waypoint, temp;
	gentity_t* hit;
	qboolean destination = qfalse;
	const int ourHealth = g_entities[bs->client].health;

	// ========================================
	// SIMPLE FIX: FORCE NAVIGATION WHEN NO VISIBLE ENEMIES
	// ========================================
	qboolean shouldNavigate = qfalse;

	// Force navigation if no current enemy
	if (!bs->currentEnemy) {
		shouldNavigate = qtrue;
	}
	// Force navigation if enemy is not visible
	else if (!bs->frame_Enemy_Vis) {
		shouldNavigate = qtrue;
	}
	// Force navigation if enemy is far away
	else if (bs->frame_Enemy_Len > 1024) {
		shouldNavigate = qtrue;
	}

	// If we should navigate, clear enemy to prevent conflicts
	if (shouldNavigate && bs->currentEnemy) {
		// Don't completely clear enemy, just ignore it for navigation
		bs->frame_Enemy_Vis = 0;
	}

	if ((bs->cur_ps.weapon != WP_SABER) && BotWeaponSelectable(bs, WP_SABER))
		BotSelectWeapon(bs->client, WP_SABER);

	// Cancel speed if FP is 0
	if ((bs->cur_ps.fd.forcePowersActive & (1 << FP_SPEED)) && bs->cur_ps.fd.forcePower <= 0) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_SPEED;
		trap->EA_ForcePower(bs->client);
	}

	// Check FP level before using force powers
	if (bs->cur_ps.fd.forcePower < 10) {
		useTheForce = qfalse;
	}
	else {
		if (bs->cur_ps.fd.forcePowersActive & (1 << FP_SPEED)) {
			useTheForce = qfalse;
		}
		else if (bs->cur_ps.fd.forcePowersActive & (1 << FP_RAGE)) {
			if (ourHealth >= 30) {
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_RAGE;
				useTheForce = qtrue;
			}
			else {
				useTheForce = qfalse;
			}
		}
		else if (bs->cur_ps.fd.forceSide == FORCE_LIGHTSIDE) {
			if (bs->cur_ps.fd.forcePowersActive & (1 << FP_PROTECT)) {
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_PROTECT;
				useTheForce = qtrue;
			}
			else if (bs->cur_ps.fd.forcePowersActive & (1 << FP_ABSORB)) {
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_ABSORB;
				useTheForce = qtrue;
			}
			else if ((bs->cur_ps.fd.forcePowersKnown & (1 << FP_HEAL)) && (ourHealth < 100) && (bs->cur_ps.fd.forcePower > 50)) {
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_HEAL;
				useTheForce = qtrue;
			}
		}
		else if (bs->cur_ps.fd.forceSide == FORCE_DARKSIDE) {
			// Dark side - no force powers used when alone
		}
	}

	if (useTheForce && (level.time % 1000 > 500))
		trap->EA_ForcePower(bs->client);

	// ========================================
	// PRIORITY 1: WAYPOINT NAVIGATION (FORCED WHEN SHOULD NAVIGATE)
	// ========================================
	if (shouldNavigate || !bs->wpCurrent) {
		if (!bs->wpCurrent) {
			int wp = GetNearestVisibleWP(bs->origin, bs->client);
			if (wp != -1) {
				bs->wpCurrent = gWPArray[wp];
				bs->wpSeenTime = level.time + 1500;
				bs->wpTravelTime = level.time + 10000;
			}
		}

		if (bs->wpCurrent) {
			VectorCopy(bs->wpCurrent->origin, waypoint);
			destination = qtrue;

			// Handle waypoint touching
			vec3_t wpDist;
			VectorSubtract(bs->wpCurrent->origin, bs->origin, wpDist);
			if (VectorLength(wpDist) < BOT_WPTOUCH_DISTANCE) {
				WPTouchRoutine(bs);

				// Get next waypoint
				int desiredIndex = (!bs->wpDirection) ?
					bs->wpCurrent->index + 1 : bs->wpCurrent->index - 1;

				if (gWPArray[desiredIndex] && gWPArray[desiredIndex]->inuse &&
					PassWayCheck(bs, desiredIndex)) {
					bs->wpCurrent = gWPArray[desiredIndex];
				}
				else {
					bs->wpDirection = !bs->wpDirection;
				}
			}

			WPConstantRoutine(bs);
		}
	}

	// ========================================
	// PRIORITY 2: ITEM COLLECTION (ONLY IF NO WAYPOINT)
	// ========================================
	if (!destination) {
		VectorAdd(maxs, bs->origin, maxs);
		VectorAdd(mins, bs->origin, mins);

		numEnts = trap->EntitiesInBox(mins, maxs, radiusEnts, 256);
		for (i = 0; i < numEnts; i++) {
			int weapon, ammo;
			hit = &g_entities[radiusEnts[i]];

			if (hit->s.eType != ET_ITEM)
				continue;

			if (hit->nextthink)
				continue;

			if (hit->item->giType == IT_POWERUP) {
				if (bs->cur_ps.fd.forceSide == FORCE_LIGHTSIDE) {
					if (hit->item->giTag == PW_FORCE_ENLIGHTENED_DARK)
						continue;
				}
				else {
					if (hit->item->giTag == PW_FORCE_ENLIGHTENED_LIGHT)
						continue;
				}
			}

			if (hit->item->giType == IT_WEAPON) {
				weapon = hit->item->giTag;
				if (bs->cur_ps.stats[STAT_WEAPONS] & (1 << weapon))
					continue;
			}

			if (hit->item->giType == IT_AMMO) {
				ammo = hit->item->giTag;
				switch (ammo) {
				case AMMO_BLASTER:
				case AMMO_POWERCELL:
				case AMMO_METAL_BOLTS:
					if (bs->cur_ps.ammo[ammo] >= 300)
						continue;
					break;
				case AMMO_ROCKETS:
					if (bs->cur_ps.ammo[ammo] >= 25)
						continue;
					break;
				default:
					continue;
				}
			}

			if ((hit->item->giType == IT_HEALTH) && (ourHealth >= 100))
				continue;

			if ((hit->item->giType == IT_ARMOR) && (bs->cur_ps.stats[STAT_ARMOR] >= 100))
				continue;

			if (hit->item->giType == IT_HOLDABLE)
				continue;

			VectorCopy(hit->s.origin, waypoint);
			destination = qtrue;
			break;
		}
	}

	// ========================================
	// PRIORITY 3: ENEMY TRACKING (ONLY IF NO WAYPOINT/ITEMS)
	// ========================================
	if (!destination && bs->currentEnemy && !bs->frame_Enemy_Vis) {
		if (bs->lastVisibleEnemyIndex == bs->currentEnemy->s.number &&
			bs->lastVisibleEnemyIndex > level.time - 10000) {

			VectorCopy(bs->lastEnemySpotted, waypoint);
			destination = qtrue;

			VectorSubtract(bs->lastEnemySpotted, bs->eye, temp);
			vectoangles(temp, bs->ideal_viewangles);

			if (Q_irand(1, 100) <= 20) {
				bs->ideal_viewangles[YAW] += Q_irand(-45, 45);
				bs->ideal_viewangles[PITCH] += Q_irand(-10, 10);
			}
		}
	}

	// ========================================
	// SIMPLE FENCE/WALL JUMP SYSTEM
	// ========================================
	if (destination) {
		vec3_t mins2, maxs2, forward, end, start, checkPos;
		trace_t tr;

		VectorSet(mins2, -15, -15, -8);
		VectorSet(maxs2, 15, 15, 32);

		VectorSubtract(waypoint, bs->origin, temp);
		VectorNormalize(temp);
		VectorCopy(temp, forward);

		// SIMPLE FENCE/WALL DETECTION
		VectorMA(bs->origin, 48, forward, end);
		JP_Trace(&tr, bs->origin, mins2, maxs2, end, bs->client, MASK_SOLID, qfalse, 0, 0);

		if (tr.fraction < 0.9) { // Any obstacle detected
			// Check if it's a fence/low wall (jumpable)
			VectorCopy(bs->origin, start);
			start[2] += 24; // Jump height for fence

			VectorCopy(end, checkPos);
			checkPos[2] += 24;

			JP_Trace(&tr, start, mins2, maxs2, checkPos, bs->client, MASK_SOLID, qfalse, 0, 0);

			if (tr.fraction > 0.7) { // Can jump over obstacle
				if (bs->jumpTime < level.time) {
					bs->jumpTime = level.time + 1500; // 1.5 second cooldown
					trap->EA_Jump(bs->client);
				}
			}
			else {
				// Try higher jump for walls
				start[2] += 16; // Total 40 units height
				checkPos[2] += 16;

				JP_Trace(&tr, start, mins2, maxs2, checkPos, bs->client, MASK_SOLID, qfalse, 0, 0);

				if (tr.fraction > 0.6) { // Can jump over wall
					if (bs->jumpTime < level.time) {
						bs->jumpTime = level.time + 2000; // 2 second cooldown
						trap->EA_Jump(bs->client);
					}
				}
				else {
					// Try to strafe around obstacle
					if (Q_irand(1, 2) == 1) {
						trap->EA_MoveRight(bs->client);
					}
					else {
						trap->EA_MoveLeft(bs->client);
					}
				}
			}
		}

		// Check for pits
		VectorMA(bs->origin, 96, forward, end);
		end[2] -= 48;
		JP_Trace(&tr, bs->origin, mins2, maxs2, end, bs->client, MASK_SOLID, qfalse, 0, 0);

		if (tr.fraction == 1.0) { // Pit detected
			if (bs->jumpTime < level.time) {
				bs->jumpTime = level.time + 1500;
				trap->EA_Jump(bs->client);
			}
		}
	}

	// ========================================
	// FALLBACK NAVIGATION WITH FENCE/WALL JUMPS
	// ========================================
	if (!destination) {
		if (bs->cur_ps.weapon == WP_SABER) {
			if (BotFallbackNavigation(bs)) {
				return;
			}
		}

		// SIMPLE OBSTACLE CHECK FOR FALLBACK
		vec3_t mins2, maxs2, forward, end, start, checkPos;
		trace_t tr;

		VectorSet(mins2, -15, -15, -8);
		VectorSet(maxs2, 15, 15, 32);

		AngleVectors(bs->ideal_viewangles, forward, NULL, NULL);

		// Check for obstacles in movement direction
		VectorMA(bs->origin, 48, forward, end);
		JP_Trace(&tr, bs->origin, mins2, maxs2, end, bs->client, MASK_SOLID, qfalse, 0, 0);

		if (tr.fraction < 0.9) { // Obstacle detected
			// Try fence jump first
			VectorCopy(bs->origin, start);
			start[2] += 24;

			VectorCopy(end, checkPos);
			checkPos[2] += 24;

			JP_Trace(&tr, start, mins2, maxs2, checkPos, bs->client, MASK_SOLID, qfalse, 0, 0);

			if (tr.fraction > 0.7) { // Can jump over
				if (bs->jumpTime < level.time) {
					bs->jumpTime = level.time + 1500;
					trap->EA_Jump(bs->client);
				}
			}
			else {
				// Try wall jump
				start[2] += 16;
				checkPos[2] += 16;

				JP_Trace(&tr, start, mins2, maxs2, checkPos, bs->client, MASK_SOLID, qfalse, 0, 0);

				if (tr.fraction > 0.6) { // Can jump over wall
					if (bs->jumpTime < level.time) {
						bs->jumpTime = level.time + 2000;
						trap->EA_Jump(bs->client);
					}
				}
				else {
					// Turn away from obstacle
					bs->ideal_viewangles[YAW] += Q_irand(45, 135) * (Q_irand(0, 1) ? 1 : -1);
				}
			}
		}

		// Force movement when no destination
		trap->EA_MoveForward(bs->client);

		if (Q_irand(1, 100) <= 30) {
			bs->ideal_viewangles[YAW] += Q_irand(-90, 90);
		}
		else {
			bs->ideal_viewangles[YAW] += Q_irand(-2, 4);
		}

		return;
	}

	// ========================================
	// MOVEMENT TO DESTINATION
	// ========================================
	VectorSubtract(waypoint, bs->origin, temp);
	vectoangles(temp, temp);
	VectorCopy(temp, bs->ideal_viewangles);
	bs->ideal_viewangles[YAW] += 1;

	if (Q_irand(1, 10) < 2)
		trap->EA_MoveRight(bs->client);
	else if (Q_irand(1, 10) < 2)
		trap->EA_MoveLeft(bs->client);

	if ((bs->origin[2] + STEPSIZE) < waypoint[2] && (level.time % 1000 > 500))
		NewBotAI_Flipkick(bs);

	trap->EA_MoveForward(bs->client);
} // Niksata Edit - HYBRID VERSION

int NewBotAI_ScanForEnemies(bot_state_t* bs) {
	vec3_t a;
	float distcheck;
	float closest;
	int bestindex;
	int i;
	float hasEnemyDist = 0;
	qboolean noAttackNonJM = qfalse;
	int ourHealth = g_entities[bs->client].health;

	closest = 999999;
	i = 0;
	bestindex = -1;

	if (bs->currentEnemy) { //only switch to a new enemy if he's significantly closer
		hasEnemyDist = 0;
	}

	if (bs->currentEnemy && bs->currentEnemy->client && bs->currentEnemy->client->ps.isJediMaster) { //The Jedi Master must die.
		return -1;
	}

	if (level.gametype == GT_JEDIMASTER) {
		if (G_ThereIsAMaster() && !bs->cur_ps.isJediMaster) { //if friendly fire is on in jedi master we can attack people that bug us
			if (!g_friendlyFire.value) {
				noAttackNonJM = qtrue;
			}
			else {
				closest = 128; //only get mad at people if they get close enough to you to anger you, or hurt you
			}
		}
	}

	//for (i = 0; i < level.numConnectedClients; i++) { //Go through each client, see if they are "afk", if everyone is afk, fuck this then.
	for (i = 0; i < MAX_CLIENTS; i++) { //Go through each client, see if they are "afk", if everyone is afk, fuck this then.
		//gentity_t* ent = &g_entities[level.sortedClients[i]];
		gentity_t* ent = &g_entities[i];

		if (ent && ent->inuse && PassStandardEnemyChecks(bs, ent) && BotPVSCheck(ent->client->ps.origin, bs->eye) && PassLovedOneCheck(bs, ent)) {
			float normalizedHealth = 0.25 + (ent->health - 1) * (1 - 0.25) / (100 - 1); //Range .25 to 1
			if (ent->client->ps.fd.forceGripEntityNum == bs->cur_ps.clientNum) { //always aim at whos gripping us
				bestindex = i;
				break;
			}

			VectorSubtract(ent->client->ps.origin, bs->eye, a);

			normalizedHealth += (100 - ourHealth) * 0.005; //Bring normalizedhealth closer to 1 the lower HP we ourselves are?, up to +0.5?
			if (normalizedHealth > 1)
				normalizedHealth = 1;

			//See if we have a LOS to them.  If not, scale the distcheck way up.. expensive?

			distcheck = VectorLength(a) * normalizedHealth;
			vectoangles(a, a);

			if (ent->client->ps.isJediMaster) { //make us think the Jedi Master is close so we'll attack him above all
				distcheck = 1;
			}

			if (distcheck < closest) {
				if (BotMindTricked(bs->client, i)) {
					if (distcheck < 256 || (level.time - ent->client->dangerTime) < 100) {
						if (!hasEnemyDist || distcheck < (hasEnemyDist - 128)) { //if we have an enemy, only switch to closer if he is 128+ closer to avoid flipping out
							if (!noAttackNonJM || ent->client->ps.isJediMaster) {
								closest = distcheck;
								bestindex = i;
							}
						}
					}
				}
				else {
					if (!hasEnemyDist || distcheck < (hasEnemyDist - 128)) {//if we have an enemy, only switch to closer if he is 128+ closer to avoid flipping out
						if (!noAttackNonJM || ent->client->ps.isJediMaster) {
							closest = distcheck;
							bestindex = i;
						}
					}
				}
			}
		}
	}
	return bestindex;
}

#define _ADVANCEDBOTSHIT 1

void NewBotAI(bot_state_t* bs, float thinktime) //BOT START // Niksata Edit - FIXED VERSION
{
	int closestID = -1;
	int i;
	qboolean someonesHere = qfalse;
	vec3_t headlevel, a, ang, a_fo;
	float mLen;
	int meleestrafe = 0;

	bs->isCamper = 0; //reset this

	if (bs->cur_ps.stats[STAT_RACEMODE]) {
		return;
	}

	if (g_entities[bs->client].health < 1) { //We are dead, so respawn!
		trap->EA_Attack(bs->client);
		return;
	}

	if (g_entities[bs->client].client->pers.amfreeze) //No AI if we are frozen
		return;

	if (g_newBotAITarget.integer < 0)
		closestID = NewBotAI_ScanForEnemies(bs); //This has been modified to take health into account, and ignore FOV, mindtrick, etc, when newBotAI is being used.
	else {
		gclient_t* cl;
		closestID = g_newBotAITarget.integer;

		if (closestID < 0 || closestID >= level.maxclients)
			closestID = -1;

		cl = &level.clients[closestID];
		if (!cl || cl->pers.connected != CON_CONNECTED)//Or in spectate? or?
			closestID = -1;
	}

	if (g_movementStyle.integer == MV_TRIBES) { //&& CAPPING?
		if (closestID == -1 && (!g_entities[bs->client].client || !g_entities[bs->client].client->pers.activeCapRoute)) { //if we have no active route and no1 near, suicid
			if ((redRouteList[0].length && g_entities[bs->client].client->sess.sessionTeam == TEAM_RED) || (blueRouteList[0].length && g_entities[bs->client].client->sess.sessionTeam == TEAM_BLUE)) { //only if map actually has cap routes do we behave like they have cap routes
				G_Kill(&g_entities[bs->client]);
				return;
			}
		}
		if (NewBotAI_CapRoute(bs, thinktime))
			return;
	}

	if (closestID == -1) {//Its just us, or they are too far away.
#if _ADVANCEDBOTSHIT
		NewBotAI_DoAloneStuff(bs, thinktime);
#else
		//StandardBotAI(bs, thinktime);
#endif
		return;
	}

	bs->frame_Enemy_Vis = 0;
	VectorCopy(g_entities[closestID].client->ps.origin, headlevel);
	headlevel[2] += g_entities[closestID].client->ps.viewheight - 24;

	if ((bs->cur_ps.weapon == WP_DEMP2 && g_entities[bs->client].client->forcedFireMode != 1) || (g_newBotAITarget.integer >= 0) || OrgVisible(bs->eye, g_entities[closestID].client->ps.origin, bs->client)) { //We can see or dmg our closest enemy
		bs->currentEnemy = &g_entities[closestID];
		bs->frame_Enemy_Vis = 1;
		bs->lastVisibleEnemyIndex = level.time;
	}
	else { //we can't see our closest enemy, use last attacker
		const int attacker = g_entities[bs->client].client->ps.persistant[PERS_ATTACKER];
		if (PassStandardEnemyChecks(bs, &g_entities[attacker])) {
			bs->currentEnemy = &g_entities[attacker];

			VectorCopy(bs->currentEnemy->client->ps.origin, headlevel);
			headlevel[2] += bs->currentEnemy->client->ps.viewheight - 24;
			if (OrgVisible(bs->eye, headlevel, bs->client))
				bs->frame_Enemy_Vis = 1;
			bs->lastVisibleEnemyIndex = level.time;
		}
		else {
			bs->currentEnemy = &g_entities[closestID];
			if (bs->lastVisibleEnemyIndex < level.time - 10000) { //let him keep going for target for 10s before abandoning
				NewBotAI_DoAloneStuff(bs, thinktime);
				return;
			}
		}
	}

	bs->enemySeenTime = level.time + ENEMY_FORGET_MS;
	bs->frame_Enemy_Len = NewBotAI_GetDist(bs);

	if (!bs->frame_Enemy_Vis && bs->frame_Enemy_Len > 300) { // Niksata Edit
#if _ADVANCEDBOTSHIT
		NewBotAI_DoAloneStuff(bs, thinktime);
#else
		//StandardBotAI(bs, thinktime);
#endif
		return;
	}

	if (g_movementStyle.integer == MV_TRIBES && (g_startingItems.integer & (1 << HI_JETPACK))) {
		NewBotAI_Tribes(bs, thinktime);
		return;
	}

	// ========================================
	// COMBAT SYSTEM - UNIFIED MOVEMENT HANDLING
	// ========================================
	if ((g_forcePowerDisable.integer != 163837 && g_forcePowerDisable.integer != 163839) || (g_flipKick.integer) || (bs->cur_ps.weapon != WP_SABER)) {
		if (bs->currentEnemy->client->ps.fd.forceSide == FORCE_LIGHTSIDE) { // They are LS.
			if (bs->cur_ps.fd.forceSide == FORCE_LIGHTSIDE)
				NewBotAI_LSvLS(bs);  // KEEP SEPARATE
			else
				NewBotAI_DSvLS(bs);  // KEEP SEPARATE
		}
		else { // A sith is amongst us!
			if (bs->cur_ps.fd.forceSide == FORCE_LIGHTSIDE)
				NewBotAI_LSvDS(bs);  // KEEP SEPARATE
			else
				NewBotAI_DSvDS(bs);  // KEEP SEPARATE
		}

		// ========================================
		// UNIFIED COMBAT MOVEMENT SYSTEM
		// ========================================
		// FIXED: Handle all combat movement in ONE place to prevent conflicts
		if (bs->currentEnemy && bs->frame_Enemy_Vis) {
			if (bs->currentEnemy) {
				if (BotGetWeaponRange(bs) == BWEAPONRANGE_SABER) {
					int saberRange = SABER_ATTACK_RANGE;

					VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, a_fo);
					vectoangles(a_fo, a_fo);

					// ================================
					// YOUR INTELLIGENT STYLE SELECTION
					// ================================
					Bot_SelectOptimalSaberStyle(bs);

					// ================================
					// YOUR PERFECT BLOCKING SYSTEM
					// ================================
					if (bs->frame_Enemy_Len <= saberRange) {
						// ENHANCED COMBAT SYSTEM
						BotUpdateAttackTiming(bs);
						BotUpdateDynamicMovement(bs);

						// Check if we should attack
						if (BotShouldAttackNow(bs)) {
							BotExecuteAttack(bs);
						}
					}

					// ================================
					// KEEP ORIGINAL SABER THROW LOGIC
					// ================================
					if (level.gametype == GT_SINGLE_PLAYER) {
						saberRange *= 3;
					}

					if (bs->saberThrowTime < level.time && !bs->cur_ps.saberInFlight &&
						(bs->cur_ps.fd.forcePowersKnown & (1 << FP_SABERTHROW)) &&
						InFieldOfVision(bs->viewangles, 30, a_fo) &&
						bs->frame_Enemy_Len < 900 &&
						bs->frame_Enemy_Len > 450 &&
						bs->cur_ps.fd.saberAnimLevel != SS_STAFF) {
						bs->doAltAttack = 1;
						bs->doAttack = 0;
					}
					else if (bs->cur_ps.saberInFlight && bs->frame_Enemy_Len > 4500 && bs->frame_Enemy_Len < 900) {
						bs->doAltAttack = 1;
						bs->doAttack = 0;
					}
				}
				// ================================
				// KEEP ORIGINAL MELEE LOGIC
				// ================================
				else if (BotGetWeaponRange(bs) == BWEAPONRANGE_MELEE) {
					if (bs->frame_Enemy_Len <= MELEE_ATTACK_RANGE) {
						MeleeCombatHandling(bs);
						meleestrafe = 1;
					}
				}
			}

			// ========================================
			// INTELLIGENT COMBAT MOVEMENT - UNIFIED
			// ========================================
			float enemyDist = bs->frame_Enemy_Len;
			vec3_t forward, right, mins, maxs, end;
			trace_t tr;
			qboolean canMoveForward = qtrue;
			qboolean canMoveBack = qtrue;
			qboolean canMoveLeft = qtrue;
			qboolean canMoveRight = qtrue;

			// Check movement directions
			VectorSet(mins, -15, -15, -8);
			VectorSet(maxs, 15, 15, 32);
			AngleVectors(bs->viewangles, forward, right, NULL);

			// Check forward
			VectorMA(bs->origin, 32, forward, end);
			JP_Trace(&tr, bs->origin, mins, maxs, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);
			if (tr.fraction < 0.8) canMoveForward = qfalse;

			// Check backward
			VectorMA(bs->origin, -32, forward, end);
			JP_Trace(&tr, bs->origin, mins, maxs, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);
			if (tr.fraction < 0.8) canMoveBack = qfalse;

			// Check left
			VectorMA(bs->origin, -32, right, end);
			JP_Trace(&tr, bs->origin, mins, maxs, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);
			if (tr.fraction < 0.8) canMoveLeft = qfalse;

			// Check right
			VectorMA(bs->origin, 32, right, end);
			JP_Trace(&tr, bs->origin, mins, maxs, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);
			if (tr.fraction < 0.8) canMoveRight = qfalse;

			// INTELLIGENT DISTANCE-BASED MOVEMENT
			if (enemyDist < 64) { // Very close - tactical retreat
				if (canMoveBack) {
					trap->EA_MoveBack(bs->client);
				}
				else if (canMoveLeft && canMoveRight) {
					// Strafe away if can't back up
					if (bs->meleeStrafeDir && canMoveRight) {
						trap->EA_MoveRight(bs->client);
					}
					else if (!bs->meleeStrafeDir && canMoveLeft) {
						trap->EA_MoveLeft(bs->client);
					}
				}
			}
			else if (enemyDist < 128) { // Close range - circle strafe
				if (Q_irand(1, 10) <= 6) { // 60% chance to strafe
					if (bs->meleeStrafeDir && canMoveRight) {
						trap->EA_MoveRight(bs->client);
					}
					else if (!bs->meleeStrafeDir && canMoveLeft) {
						trap->EA_MoveLeft(bs->client);
					}
					else if (canMoveForward) {
						// If preferred strafe is blocked, move forward
						trap->EA_MoveForward(bs->client);
					}
				}
				else if (canMoveForward && Q_irand(1, 10) <= 3) { // 30% chance to advance
					trap->EA_MoveForward(bs->client);
				}
			}
			else if (enemyDist < 256) { // Medium range - approach cautiously
				if (canMoveForward) {
					trap->EA_MoveForward(bs->client);
				}
				else if (Q_irand(1, 10) <= 4) { // 40% chance to strafe if blocked
					if (bs->meleeStrafeDir && canMoveRight) {
						trap->EA_MoveRight(bs->client);
					}
					else if (!bs->meleeStrafeDir && canMoveLeft) {
						trap->EA_MoveLeft(bs->client);
					}
				}
			}
			else { // Long range - direct approach
				if (canMoveForward) {
					trap->EA_MoveForward(bs->client);
				}
				else if (canMoveLeft) {
					trap->EA_MoveLeft(bs->client);
				}
				else if (canMoveRight) {
					trap->EA_MoveRight(bs->client);
				}
			}

			// Random direction change (less frequent)
			if (Q_irand(1, 200) <= 3) { // 1.5% chance
				bs->meleeStrafeDir = !bs->meleeStrafeDir;
			}
		}
	}
	else {//Ruh roh, NF with no kick!
		NewBotAI_NF(bs);
	}

	// ========================================
	// COMBAT TIMING AND AIMING SYSTEM - ENHANCED
	// ========================================
	if (bs->timeToReact < level.time && bs->currentEnemy && bs->enemySeenTime > level.time + (ENEMY_FORGET_MS - (ENEMY_FORGET_MS * 0.2)))
	{
		if (bs->frame_Enemy_Vis)
		{
			// ENHANCED ATTACK - NO DUPLICATE MOVEMENT
			NewBotAI_GetAttack(bs); // Keep original attack logic

			if (bs->destinationGrabTime > level.time + 100)
			{
				bs->destinationGrabTime = level.time + 100; //assures that we will continue staying within a general area of where we want to be in a combat situation
			}

			if (bs->currentEnemy->client)
			{
				VectorCopy(bs->currentEnemy->client->ps.origin, headlevel);
				headlevel[2] += bs->currentEnemy->client->ps.viewheight - 24;
			}
			else
			{
				VectorCopy(bs->currentEnemy->client->ps.origin, headlevel);
			}

			if (!bs->frame_Enemy_Vis)
			{
				//if (!bs->hitSpotted && VectorLength(a) > 256)
				if (OrgVisible(bs->eye, bs->lastEnemySpotted, -1))
				{
					VectorCopy(bs->lastEnemySpotted, headlevel);
					VectorSubtract(headlevel, bs->eye, a);
					vectoangles(a, ang);
					VectorCopy(ang, bs->goalAngles);

					if (bs->cur_ps.weapon == WP_FLECHETTE &&
						bs->cur_ps.weaponstate == WEAPON_READY &&
						bs->currentEnemy && bs->currentEnemy->client)
					{
						mLen = VectorLength(a) > 128;
						if (mLen > 128 && mLen < 1024)
						{
							VectorSubtract(bs->currentEnemy->client->ps.origin, bs->lastEnemySpotted, a);

							if (VectorLength(a) < 300)
							{
								bs->doAltAttack = 1;
							}
						}
					}
				}
			}
			else
			{
				// ENHANCED AIMING SYSTEM
				if (bs->currentEnemy && bs->frame_Enemy_Vis) {
					vec3_t predictedPos;
					BotPredictEnemyPosition(bs, predictedPos);
					VectorSubtract(predictedPos, bs->eye, a);
					vectoangles(a, ang);
					VectorCopy(ang, bs->goalAngles);
				}
				else {
					VectorSubtract(headlevel, bs->eye, a);
					vectoangles(a, ang);
					VectorCopy(ang, bs->goalAngles);
				}

				BotAimOffsetGoalAngles(bs);
			}
		}
		else if (bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT)
		{ //keep charging in case we see him again before we lose track of him
			bs->doAltAttack = 1;
		}
		else if (bs->cur_ps.weaponstate == WEAPON_CHARGING)
		{ //keep charging in case we see him again before we lose track of him
			bs->doAttack = 1;
		}
	}

	if (bs->cur_ps.saberInFlight)
	{
		bs->saberThrowTime = level.time + Q_irand(4000, 10000);
	}

	// ========================================
	// SIMPLE OBSTACLE HANDLING - NO DISRUPTION
	// ========================================

	// NEW: Simple obstacle interaction call
	//Bot_CheckAndInteractWithObstacle(bs);

	// ========================================
	// REMOVED: DUPLICATE STRAFING SYSTEM
	// ========================================
	// FIXED: Removed the duplicate strafing system that was causing conflicts
	// All combat movement is now handled in the unified system above
} // Niksata Edit - FIXED VERSION


//the main AI loop.
//please don't be too frightened.
void StandardBotAI(bot_state_t* bs, float thinktime)
{
	int wp, enemy;
	int desiredIndex;
	int goalWPIndex;
	int doingFallback = 0;
	int fjHalt;
	vec3_t a, ang, headlevel, eorg, noz_x, noz_y, dif, a_fo;
	float reaction;
	float bLeadAmount;
	int meleestrafe = 0;
	int useTheForce = 0;
	int forceHostile = 0;
	gentity_t* friendInLOF = 0;
	float mLen;
	int visResult = 0;
	int selResult = 0;
	int mineSelect = 0;
	int detSelect = 0;
	vec3_t preFrameGAngles;

	static vec3_t lastStuckCheckPos;
	static int lastStuckCheckTime = 0;
	static int stuckCounter = 0;
	static vec3_t lastUnstuckDirection;
	static int lastBackupTime = 0;
	static int consecutiveBackups = 0;

	if (gDeactivated)
	{
		bs->wpCurrent = NULL;
		bs->currentEnemy = NULL;
		bs->wpDestination = NULL;
		bs->wpDirection = 0;
		return;
	}

	if (g_entities[bs->client].inuse &&
		g_entities[bs->client].client &&
		g_entities[bs->client].client->sess.sessionTeam == TEAM_SPECTATOR)
	{
		bs->wpCurrent = NULL;
		bs->currentEnemy = NULL;
		bs->wpDestination = NULL;
		bs->wpDirection = 0;
		return;
	}


#ifndef FINAL_BUILD
	if (bot_getinthecarrr.integer)
	{ //stupid vehicle debug, I tire of having to connect another client to test passengers.
		gentity_t* botEnt = &g_entities[bs->client];

		if (botEnt->inuse && botEnt->client && botEnt->client->ps.m_iVehicleNum)
		{ //in a vehicle, so...
			bs->noUseTime = level.time + 5000;

			if (bot_getinthecarrr.integer != 2)
			{
				trap->EA_MoveForward(bs->client);

				if (bot_getinthecarrr.integer == 3)
				{ //use alt fire
					trap->EA_Alt_Attack(bs->client);
				}
			}
		}
		else
		{ //find one, get in
			int i = 0;
			gentity_t* vehicle = NULL;
			//find the nearest, manned vehicle
			while (i < MAX_GENTITIES)
			{
				vehicle = &g_entities[i];

				if (vehicle->inuse && vehicle->client && vehicle->s.eType == ET_NPC &&
					vehicle->s.NPC_class == CLASS_VEHICLE && vehicle->m_pVehicle &&
					(vehicle->client->ps.m_iVehicleNum || bot_getinthecarrr.integer == 2))
				{ //ok, this is a vehicle, and it has a pilot/passengers
					break;
				}
				i++;
			}
			if (i != MAX_GENTITIES && vehicle)
			{ //broke before end so we must've found something
				vec3_t v;

				VectorSubtract(vehicle->client->ps.origin, bs->origin, v);
				VectorNormalize(v);
				vectoangles(v, bs->goalAngles);
				MoveTowardIdealAngles(bs);
				trap->EA_Move(bs->client, v, 5000.0f);

				if (bs->noUseTime < (level.time - 400))
				{
					bs->noUseTime = level.time + 500;
				}
			}
		}

		return;
	}
#endif

	if (bot_forgimmick.integer)
	{
		bs->wpCurrent = NULL;
		bs->currentEnemy = NULL;
		bs->wpDestination = NULL;
		bs->wpDirection = 0;

		if (bot_forgimmick.integer == 2)
		{ //for debugging saber stuff, this is handy
			trap->EA_Attack(bs->client);
		}

		if (bot_forgimmick.integer == 3)
		{ //for testing cpu usage moving around rmg terrain without AI
			vec3_t mdir;

			VectorSubtract(bs->origin, vec3_origin, mdir);
			VectorNormalize(mdir);
			trap->EA_Attack(bs->client);
			trap->EA_Move(bs->client, mdir, 5000);
		}

		if (bot_forgimmick.integer == 4)
		{ //constantly move toward client 0
			if (g_entities[0].client && g_entities[0].inuse)
			{
				vec3_t mdir;

				VectorSubtract(g_entities[0].client->ps.origin, bs->origin, mdir);
				VectorNormalize(mdir);
				trap->EA_Move(bs->client, mdir, 5000);
			}
		}

		if (bs->forceMove_Forward)
		{
			if (bs->forceMove_Forward > 0)
			{
				trap->EA_MoveForward(bs->client);
			}
			else
			{
				trap->EA_MoveBack(bs->client);
			}
		}
		if (bs->forceMove_Right)
		{
			if (bs->forceMove_Right > 0)
			{
				trap->EA_MoveRight(bs->client);
			}
			else
			{
				trap->EA_MoveLeft(bs->client);
			}
		}
		if (bs->forceMove_Up)
		{
			trap->EA_Jump(bs->client);
		}
		return;
	}

	if (!bs->lastDeadTime)
	{ //just spawned in?
		bs->lastDeadTime = level.time;
	}

	if (g_entities[bs->client].health < 1)
	{
		bs->lastDeadTime = level.time;

		if (!bs->deathActivitiesDone && bs->lastHurt && bs->lastHurt->client && bs->lastHurt->s.number != bs->client)
		{
			BotDeathNotify(bs);
			if (PassLovedOneCheck(bs, bs->lastHurt))
			{
				//CHAT: Died
				bs->chatObject = bs->lastHurt;
				bs->chatAltObject = NULL;
				BotDoChat(bs, "Died", 0);
			}
			else if (!PassLovedOneCheck(bs, bs->lastHurt) &&
				botstates[bs->lastHurt->s.number] &&
				PassLovedOneCheck(botstates[bs->lastHurt->s.number], &g_entities[bs->client]))
			{ //killed by a bot that I love, but that does not love me
				bs->chatObject = bs->lastHurt;
				bs->chatAltObject = NULL;
				BotDoChat(bs, "KilledOnPurposeByLove", 0);
			}

			bs->deathActivitiesDone = 1;
		}

		bs->wpCurrent = NULL;
		bs->currentEnemy = NULL;
		bs->wpDestination = NULL;
		bs->wpCamping = NULL;
		bs->wpCampingTo = NULL;
		bs->wpStoreDest = NULL;
		bs->wpDestIgnoreTime = 0;
		bs->wpDestSwitchTime = 0;
		bs->wpSeenTime = 0;
		bs->wpDirection = 0;

		if (rand() % 10 < 5 &&
			(!bs->doChat || bs->chatTime < level.time))
		{
			trap->EA_Attack(bs->client);
		}

		return;
	}

	VectorCopy(bs->goalAngles, preFrameGAngles);

	bs->doAttack = 0;
	bs->doAltAttack = 0;
	//reset the attack states

	if (bs->isSquadLeader)
	{
		CommanderBotAI(bs);
	}
	else
	{
		BotDoTeamplayAI(bs);
	}

	if (!bs->currentEnemy)
	{
		bs->frame_Enemy_Vis = 0;
	}

	if (bs->revengeEnemy && bs->revengeEnemy->client &&
		bs->revengeEnemy->client->pers.connected != CON_CONNECTED && bs->revengeEnemy->client->pers.connected != CON_CONNECTING)
	{
		bs->revengeEnemy = NULL;
		bs->revengeHateLevel = 0;
	}

	if (bs->currentEnemy && bs->currentEnemy->client &&
		bs->currentEnemy->client->pers.connected != CON_CONNECTED && bs->currentEnemy->client->pers.connected != CON_CONNECTING)
	{
		bs->currentEnemy = NULL;
	}

	fjHalt = 0;

#ifndef FORCEJUMP_INSTANTMETHOD
	if (bs->forceJumpChargeTime > level.time)
	{
		useTheForce = 1;
		forceHostile = 0;
	}

	if (bs->currentEnemy && bs->currentEnemy->client && bs->frame_Enemy_Vis && bs->forceJumpChargeTime < level.time) // Niksata Edit
#else
	if (bs->currentEnemy && bs->currentEnemy->client && bs->frame_Enemy_Vis)
#endif
		if ((g_forcePowerDisable.integer != 163837 && g_forcePowerDisable.integer != 163839) || (g_flipKick.integer) || (bs->cur_ps.weapon != WP_SABER)) {
			// Then do normal combat
			if (bs->currentEnemy->client->ps.fd.forceSide == FORCE_LIGHTSIDE) { // They are LS.
				if (bs->cur_ps.fd.forceSide == FORCE_LIGHTSIDE)
					NewBotAI_LSvLS(bs);
				else
					NewBotAI_DSvLS(bs);
			}
			else { // A sith is amongst us!
				if (bs->cur_ps.fd.forceSide == FORCE_LIGHTSIDE)
					NewBotAI_LSvDS(bs);
				else
					NewBotAI_DSvDS(bs);
			}
		} // Niksata Edit

	doingFallback = 0;

	bs->deathActivitiesDone = 0;

	if (BotUseInventoryItem(bs))
	{
		if (rand() % 10 < 5)
		{
			trap->EA_Use(bs->client);
		}
	}

	if (bs->cur_ps.ammo[weaponData[bs->cur_ps.weapon].ammoIndex] < weaponData[bs->cur_ps.weapon].energyPerShot)
	{
		if (BotTryAnotherWeapon(bs))
		{
			return;
		}
	}
	else
	{
		if (bs->currentEnemy && bs->lastVisibleEnemyIndex == bs->currentEnemy->s.number &&
			bs->frame_Enemy_Vis && bs->forceWeaponSelect /*&& bs->plantContinue < level.time*/)
		{
			bs->forceWeaponSelect = 0;
		}

		if (bs->plantContinue > level.time)
		{
			bs->doAttack = 1;
			bs->destinationGrabTime = 0;
		}

		if (!bs->forceWeaponSelect && bs->cur_ps.hasDetPackPlanted && bs->plantKillEmAll > level.time)
		{
			bs->forceWeaponSelect = WP_DET_PACK;
		}

		if (bs->forceWeaponSelect)
		{
			selResult = BotSelectChoiceWeapon(bs, bs->forceWeaponSelect, 1);
		}

		if (selResult)
		{
			if (selResult == 2)
			{ //newly selected
				return;
			}
		}
		else if (BotSelectIdealWeapon(bs))
		{
			return;
		}
	}
	/*if (BotSelectMelee(bs))
	{
		return;
	}*/

	reaction = bs->skills.reflex / bs->settings.skill;

	if (reaction < 0)
	{
		reaction = 0;
	}
	if (reaction > 2000)
	{
		reaction = 2000;
	}

	if (!bs->currentEnemy)
	{
		bs->timeToReact = level.time + reaction;
	}

	if (bs->cur_ps.weapon == WP_DET_PACK && bs->cur_ps.hasDetPackPlanted && bs->plantKillEmAll > level.time)
	{
		bs->doAltAttack = 1;
	}

	if (bs->wpCamping)
	{
		if (bs->isCamping < level.time)
		{
			bs->wpCamping = NULL;
			bs->isCamping = 0;
		}

		if (bs->currentEnemy && bs->frame_Enemy_Vis)
		{
			bs->wpCamping = NULL;
			bs->isCamping = 0;
		}
	}

	if (bs->wpCurrent &&
		(bs->wpSeenTime < level.time || bs->wpTravelTime < level.time))
	{
		bs->wpCurrent = NULL;
	}

	if (bs->currentEnemy)
	{
		if (bs->enemySeenTime < level.time ||
			!PassStandardEnemyChecks(bs, bs->currentEnemy))
		{
			if (bs->revengeEnemy == bs->currentEnemy &&
				bs->currentEnemy->health < 1 &&
				bs->lastAttacked && bs->lastAttacked == bs->currentEnemy)
			{
				//CHAT: Destroyed hated one [KilledHatedOne section]
				bs->chatObject = bs->revengeEnemy;
				bs->chatAltObject = NULL;
				BotDoChat(bs, "KilledHatedOne", 1);
				bs->revengeEnemy = NULL;
				bs->revengeHateLevel = 0;
			}
			else if (bs->currentEnemy->health < 1 && PassLovedOneCheck(bs, bs->currentEnemy) &&
				bs->lastAttacked && bs->lastAttacked == bs->currentEnemy)
			{
				//CHAT: Killed
				bs->chatObject = bs->currentEnemy;
				bs->chatAltObject = NULL;
				BotDoChat(bs, "Killed", 0);
			}

			bs->currentEnemy = NULL;
		}
	}

	if (bot_honorableduelacceptance.integer) // Niksata Edit
	{
		if (bs->currentEnemy && bs->currentEnemy->client &&
			bs->cur_ps.weapon == WP_SABER &&
			g_privateDuel.integer &&
			bs->frame_Enemy_Vis &&
			bs->frame_Enemy_Len < 400 &&
			bs->currentEnemy->client->ps.weapon == WP_SABER &&
			bs->currentEnemy->client->ps.saberHolstered) // ONLY if enemy has holstered saber
		{
			vec3_t e_ang_vec;

			VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, e_ang_vec);

			if (InFieldOfVision(bs->viewangles, 100, e_ang_vec))
			{
				// RARE CHALLENGE (5% chance) - ONLY when enemy holstered
				if (Q_irand(1, 100) <= 5 && !bs->cur_ps.saberHolstered)
				{
					// Holster our saber first to show honor
					Cmd_ToggleSaber_f(&g_entities[bs->client]);
					bs->botChallengingTime = level.time + 100;
					bs->beStill = level.time + 100;

					if (!bs->currentEnemy->client->ps.duelInProgress &&
						!bs->cur_ps.duelInProgress)
					{
						Cmd_EngageDuel_f(&g_entities[bs->client], dueltypes[bs->currentEnemy->client->ps.clientNum]);
					}
				}
				// ACCEPT CHALLENGE IF ENEMY CHALLENGED US
				else if (bs->currentEnemy->client->ps.duelIndex == bs->client &&
					bs->currentEnemy->client->ps.duelTime > level.time &&
					!bs->cur_ps.duelInProgress)
				{
					if (!bs->cur_ps.saberHolstered)
					{
						Cmd_ToggleSaber_f(&g_entities[bs->client]);
					}
					Cmd_EngageDuel_f(&g_entities[bs->client], dueltypes[bs->currentEnemy->client->ps.clientNum]);
				}
				// TIMEOUT - RE-ENGAGE AFTER 2 SECONDS
				else if (bs->botChallengingTime > level.time &&
					bs->botChallengingTime < (level.time - 2000))
				{
					bs->botChallengingTime = 0;
					if (bs->cur_ps.saberHolstered)
					{
						Cmd_ToggleSaber_f(&g_entities[bs->client]);
					}
				}

				bs->doAttack = 0;
				bs->doAltAttack = 0;
				bs->beStill = level.time + 100;
			}
		}
	} // Niksata Edit
	//Apparently this "allows you to cheese" when fighting against bots. I'm not sure why you'd want to con bots
	//into an easy kill, since they're bots and all. But whatever.

	if (!bs->wpCurrent)
	{
		wp = GetNearestVisibleWP(bs->origin, bs->client);

		if (wp != -1)
		{
			bs->wpCurrent = gWPArray[wp];
			bs->wpSeenTime = level.time + 1500;
			bs->wpTravelTime = level.time + 10000; //never take more than 10 seconds to travel to a waypoint
		}
	}

	if (bs->enemySeenTime < level.time || !bs->frame_Enemy_Vis || !bs->currentEnemy ||
		(bs->currentEnemy /*&& bs->cur_ps.weapon == WP_SABER && bs->frame_Enemy_Len > 300*/))
	{
		enemy = ScanForEnemies(bs);

		if (enemy != -1)
		{
			bs->currentEnemy = &g_entities[enemy];
			bs->enemySeenTime = level.time + ENEMY_FORGET_MS;
			NewBotAI_DoAloneStuff(bs, thinktime);// Niksata Edit
		}
	}

	if (!bs->squadLeader && !bs->isSquadLeader)
	{
		BotScanForLeader(bs);
	}

	if (!bs->squadLeader && bs->squadCannotLead < level.time)
	{ //if still no leader after scanning, then become a squad leader
		bs->isSquadLeader = 1;
	}

	if (bs->isSquadLeader && bs->squadLeader)
	{ //we don't follow anyone if we are a leader
		bs->squadLeader = NULL;
	}

	//ESTABLISH VISIBILITIES AND DISTANCES FOR THE WHOLE FRAME HERE
	if (bs->wpCurrent)
	{
		if (RMG.integer)
		{ //this is somewhat hacky, but in RMG we don't really care about vertical placement because points are scattered across only the terrain.
			vec3_t vecB, vecC;

			vecB[0] = bs->origin[0];
			vecB[1] = bs->origin[1];
			vecB[2] = bs->origin[2];

			vecC[0] = bs->wpCurrent->origin[0];
			vecC[1] = bs->wpCurrent->origin[1];
			vecC[2] = vecB[2];


			VectorSubtract(vecC, vecB, a);
		}
		else
		{
			VectorSubtract(bs->wpCurrent->origin, bs->origin, a);
		}
		bs->frame_Waypoint_Len = VectorLength(a);

		visResult = WPOrgVisible(&g_entities[bs->client], bs->origin, bs->wpCurrent->origin, bs->client);

		if (visResult == 2)
		{
			bs->frame_Waypoint_Vis = 0;
			bs->wpSeenTime = 0;
			bs->wpDestination = NULL;
			bs->wpDestIgnoreTime = level.time + 5000;

			if (bs->wpDirection)
			{
				bs->wpDirection = 0;
			}
			else
			{
				bs->wpDirection = 1;
			}
		}
		else if (visResult)
		{
			bs->frame_Waypoint_Vis = 1;
		}
		else
		{
			bs->frame_Waypoint_Vis = 0;
		}
	}

	if (bs->currentEnemy)
	{
		if (bs->currentEnemy->client)
		{
			VectorCopy(bs->currentEnemy->client->ps.origin, eorg);
			eorg[2] += bs->currentEnemy->client->ps.viewheight;
		}
		else
		{
			VectorCopy(bs->currentEnemy->s.origin, eorg);
		}

		VectorSubtract(eorg, bs->eye, a);
		bs->frame_Enemy_Len = VectorLength(a);

		if (OrgVisible(bs->eye, eorg, bs->client))
		{
			bs->frame_Enemy_Vis = 1;
			VectorCopy(eorg, bs->lastEnemySpotted);
			VectorCopy(bs->origin, bs->hereWhenSpotted);
			bs->lastVisibleEnemyIndex = bs->currentEnemy->s.number;
			//VectorCopy(bs->eye, bs->lastEnemySpotted);
			bs->hitSpotted = 0;
		}
		else
		{
			bs->frame_Enemy_Vis = 0;
		}
	}
	else
	{
		bs->lastVisibleEnemyIndex = ENTITYNUM_NONE;
	}
	//END

	if (bs->frame_Enemy_Vis)
	{
		bs->enemySeenTime = level.time + ENEMY_FORGET_MS;
	}

	if (bs->wpCurrent)
	{
		int wpTouchDist = BOT_WPTOUCH_DISTANCE;
		WPConstantRoutine(bs);

		if (!bs->wpCurrent)
		{ //WPConstantRoutine has the ability to nullify the waypoint if it fails certain checks, so..
			return;
		}

		if (bs->wpCurrent->flags & WPFLAG_WAITFORFUNC)
		{
			if (!CheckForFunc(bs->wpCurrent->origin, -1))
			{
				bs->beStill = level.time + 500; //no func brush under.. wait
			}
		}
		if (bs->wpCurrent->flags & WPFLAG_NOMOVEFUNC)
		{
			if (CheckForFunc(bs->wpCurrent->origin, -1))
			{
				bs->beStill = level.time + 500; //func brush under.. wait
			}
		}

		if (bs->frame_Waypoint_Vis || (bs->wpCurrent->flags & WPFLAG_NOVIS))
		{
			if (RMG.integer)
			{
				bs->wpSeenTime = level.time + 5000; //if we lose sight of the point, we have 1.5 seconds to regain it before we drop it
			}
			else
			{
				bs->wpSeenTime = level.time + 1500; //if we lose sight of the point, we have 1.5 seconds to regain it before we drop it
			}
		}
		VectorCopy(bs->wpCurrent->origin, bs->goalPosition);
		if (bs->wpDirection)
		{
			goalWPIndex = bs->wpCurrent->index - 1;
		}
		else
		{
			goalWPIndex = bs->wpCurrent->index + 1;
		}

		if (bs->wpCamping)
		{
			VectorSubtract(bs->wpCampingTo->origin, bs->origin, a);
			vectoangles(a, ang);
			VectorCopy(ang, bs->goalAngles);

			VectorSubtract(bs->origin, bs->wpCamping->origin, a);
			if (VectorLength(a) < 64)
			{
				VectorCopy(bs->wpCamping->origin, bs->goalPosition);
				bs->beStill = level.time + 1000;

				if (!bs->campStanding)
				{
					bs->duckTime = level.time + 1000;
				}
			}
		}
		else if (gWPArray[goalWPIndex] && gWPArray[goalWPIndex]->inuse &&
			!(gLevelFlags & LEVELFLAG_NOPOINTPREDICTION))
		{
			VectorSubtract(gWPArray[goalWPIndex]->origin, bs->origin, a);
			vectoangles(a, ang);
			VectorCopy(ang, bs->goalAngles);
		}
		else
		{
			VectorSubtract(bs->wpCurrent->origin, bs->origin, a);
			vectoangles(a, ang);
			VectorCopy(ang, bs->goalAngles);
		}

		if (bs->destinationGrabTime < level.time /*&& (!bs->wpDestination || (bs->currentEnemy && bs->frame_Enemy_Vis))*/)
		{
			GetIdealDestination(bs);
		}

		if (bs->wpCurrent && bs->wpDestination)
		{
			if (TotalTrailDistance(bs->wpCurrent->index, bs->wpDestination->index, bs) == -1)
			{
				bs->wpDestination = NULL;
				bs->destinationGrabTime = level.time + 10000;
			}
		}

		if (RMG.integer)
		{
			if (bs->frame_Waypoint_Vis)
			{
				if (bs->wpCurrent && !bs->wpCurrent->flags)
				{
					wpTouchDist *= 3;
				}
			}
		}

		if (bs->frame_Waypoint_Len < wpTouchDist || (RMG.integer && bs->frame_Waypoint_Len < wpTouchDist * 2))
		{
			WPTouchRoutine(bs);

			if (!bs->wpDirection)
			{
				desiredIndex = bs->wpCurrent->index + 1;
			}
			else
			{
				desiredIndex = bs->wpCurrent->index - 1;
			}

			if (gWPArray[desiredIndex] &&
				gWPArray[desiredIndex]->inuse &&
				desiredIndex < gWPNum &&
				desiredIndex >= 0 &&
				PassWayCheck(bs, desiredIndex))
			{
				bs->wpCurrent = gWPArray[desiredIndex];
			}
			else
			{
				if (bs->wpDestination)
				{
					bs->wpDestination = NULL;
					bs->destinationGrabTime = level.time + 10000;
				}

				if (bs->wpDirection)
				{
					bs->wpDirection = 0;
				}
				else
				{
					bs->wpDirection = 1;
				}
			}
		}
	}
	else //We can't find a waypoint, going to need a fallback routine.
	{
		if (bs->cur_ps.weapon == WP_SABER) // Niksata Edit
		{
			doingFallback = BotFallbackNavigation(bs);
		} // Niksata Edit
	}

	if (RMG.integer)
	{ //for RMG if the bot sticks around an area too long, jump around randomly some to spread to a new area (horrible hacky method)
		vec3_t vSubDif;

		VectorSubtract(bs->origin, bs->lastSignificantAreaChange, vSubDif);
		if (VectorLength(vSubDif) > 1500)
		{
			VectorCopy(bs->origin, bs->lastSignificantAreaChange);
			bs->lastSignificantChangeTime = level.time + 20000;
		}

		if (bs->lastSignificantChangeTime < level.time)
		{
			bs->iHaveNoIdeaWhereIAmGoing = level.time + 17000;
		}
	}

	if (bs->iHaveNoIdeaWhereIAmGoing > level.time && !bs->currentEnemy)
	{
		VectorCopy(preFrameGAngles, bs->goalAngles);
		bs->wpCurrent = NULL;
		bs->wpSwitchTime = level.time + 150;
		doingFallback = BotFallbackNavigation(bs);
		bs->jumpTime = level.time + 150;
		bs->jumpHoldTime = level.time + 150;
		bs->jDelay = 0;
		bs->lastSignificantChangeTime = level.time + 25000;
	}

	if (bs->wpCurrent && RMG.integer)
	{
		qboolean doJ = qfalse;

		if (bs->wpCurrent->origin[2] - 192 > bs->origin[2])
		{
			doJ = qtrue;
		}
		else if ((bs->wpTravelTime - level.time) < 5000 && bs->wpCurrent->origin[2] - 64 > bs->origin[2])
		{
			doJ = qtrue;
		}
		else if ((bs->wpTravelTime - level.time) < 7000 && (bs->wpCurrent->flags & WPFLAG_RED_FLAG))
		{
			if ((level.time - bs->jumpTime) > 200)
			{
				bs->jumpTime = level.time + 100;
				bs->jumpHoldTime = level.time + 100;
				bs->jDelay = 0;
			}
		}
		else if ((bs->wpTravelTime - level.time) < 7000 && (bs->wpCurrent->flags & WPFLAG_BLUE_FLAG))
		{
			if ((level.time - bs->jumpTime) > 200)
			{
				bs->jumpTime = level.time + 100;
				bs->jumpHoldTime = level.time + 100;
				bs->jDelay = 0;
			}
		}
		else if (bs->wpCurrent->index > 0)
		{
			if ((bs->wpTravelTime - level.time) < 7000)
			{
				if ((gWPArray[bs->wpCurrent->index - 1]->flags & WPFLAG_RED_FLAG) ||
					(gWPArray[bs->wpCurrent->index - 1]->flags & WPFLAG_BLUE_FLAG))
				{
					if ((level.time - bs->jumpTime) > 200)
					{
						bs->jumpTime = level.time + 100;
						bs->jumpHoldTime = level.time + 100;
						bs->jDelay = 0;
					}
				}
			}
		}

		if (doJ)
		{
			bs->jumpTime = level.time + 1500;
			bs->jumpHoldTime = level.time + 1500;
			bs->jDelay = 0;
		}
	}

	if (doingFallback)
	{
		bs->doingFallback = qtrue;
	}
	else
	{
		bs->doingFallback = qfalse;
	}

	if (bs->timeToReact < level.time && bs->currentEnemy && bs->enemySeenTime > level.time + (ENEMY_FORGET_MS - (ENEMY_FORGET_MS * 0.2)))
	{
		if (bs->frame_Enemy_Vis)
		{
			NewBotAI_GetAttack(bs); // Niksata Edit
		}
		else if (bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT)
		{ //keep charging in case we see him again before we lose track of him
			bs->doAltAttack = 1;
		}
		else if (bs->cur_ps.weaponstate == WEAPON_CHARGING)
		{ //keep charging in case we see him again before we lose track of him
			bs->doAttack = 1;
		}

		if (bs->destinationGrabTime > level.time + 100)
		{
			bs->destinationGrabTime = level.time + 100; //assures that we will continue staying within a general area of where we want to be in a combat situation
		}

		if (bs->currentEnemy->client)
		{
			VectorCopy(bs->currentEnemy->client->ps.origin, headlevel);
			headlevel[2] += bs->currentEnemy->client->ps.viewheight - 24;
		}
		else
		{
			VectorCopy(bs->currentEnemy->client->ps.origin, headlevel);
		}

		if (!bs->frame_Enemy_Vis)
		{
			//if (!bs->hitSpotted && VectorLength(a) > 256)
			if (OrgVisible(bs->eye, bs->lastEnemySpotted, -1))
			{
				VectorCopy(bs->lastEnemySpotted, headlevel);
				VectorSubtract(headlevel, bs->eye, a);
				vectoangles(a, ang);
				VectorCopy(ang, bs->goalAngles);

				if (bs->cur_ps.weapon == WP_FLECHETTE &&
					bs->cur_ps.weaponstate == WEAPON_READY &&
					bs->currentEnemy && bs->currentEnemy->client)
				{
					mLen = VectorLength(a) > 128;
					if (mLen > 128 && mLen < 1024)
					{
						VectorSubtract(bs->currentEnemy->client->ps.origin, bs->lastEnemySpotted, a);

						if (VectorLength(a) < 300)
						{
							bs->doAltAttack = 1;
						}
					}
				}
			}
		}
		else
		{
			// ENHANCED AIMING SYSTEM // Niksata Edit
			if (bs->currentEnemy && bs->frame_Enemy_Vis) {
				vec3_t predictedPos;
				BotPredictEnemyPosition(bs, predictedPos);
				VectorSubtract(predictedPos, bs->eye, a);
				vectoangles(a, ang);
				VectorCopy(ang, bs->goalAngles);
			}
			else {
				VectorSubtract(headlevel, bs->eye, a);
				vectoangles(a, ang);
				VectorCopy(ang, bs->goalAngles);
			} // Niksata Edit

			BotAimOffsetGoalAngles(bs);
		}
	}

	if (bs->cur_ps.saberInFlight)
	{
		bs->saberThrowTime = level.time + Q_irand(4000, 10000);
	}

	// ================================ // Niksata Edit
	// INTEGRATED SABER COMBAT (YOUR SYSTEM)
	// ================================
	if (bs->currentEnemy) {
		if (BotGetWeaponRange(bs) == BWEAPONRANGE_SABER) {
			int saberRange = SABER_ATTACK_RANGE;

			VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, a_fo);
			vectoangles(a_fo, a_fo);

			// ================================
			// YOUR INTELLIGENT STYLE SELECTION
			// ================================
			Bot_SelectOptimalSaberStyle(bs);

			// ================================
			// YOUR PERFECT BLOCKING SYSTEM
			// ================================
			if (bs->frame_Enemy_Len <= saberRange) { // Niksata Edit
				// ENHANCED COMBAT SYSTEM
				BotUpdateAttackTiming(bs);
				BotUpdateDynamicMovement(bs);

				// Check if we should attack
				if (BotShouldAttackNow(bs)) {
					BotExecuteAttack(bs);
				}

				// Dynamic strafing
				if (BotShouldStrafeInCombat(bs)) {
					meleestrafe = 1;
				}
				else {
					meleestrafe = 0;
				}
			} // Niksata Edit

			// ================================
			// KEEP ORIGINAL SABER THROW LOGIC
			// ================================
			if (level.gametype == GT_SINGLE_PLAYER) {
				saberRange *= 3;
			}

			if (bs->saberThrowTime < level.time && !bs->cur_ps.saberInFlight &&
				(bs->cur_ps.fd.forcePowersKnown & (1 << FP_SABERTHROW)) &&
				InFieldOfVision(bs->viewangles, 30, a_fo) &&
				bs->frame_Enemy_Len < 900 &&
				bs->frame_Enemy_Len > 450 &&
				bs->cur_ps.fd.saberAnimLevel != SS_STAFF) {
				bs->doAltAttack = 1;
				bs->doAttack = 0;
			}
			else if (bs->cur_ps.saberInFlight && bs->frame_Enemy_Len > 4500 && bs->frame_Enemy_Len < 900) {
				bs->doAltAttack = 1;
				bs->doAttack = 0;
			}
		}
		// ================================
		// KEEP ORIGINAL MELEE LOGIC
		// ================================
		else if (BotGetWeaponRange(bs) == BWEAPONRANGE_MELEE) {
			if (bs->frame_Enemy_Len <= MELEE_ATTACK_RANGE) {
				MeleeCombatHandling(bs);
				meleestrafe = 1;
			}
		}
	} // Niksata Edit

	if (bs->doChat && bs->chatTime > level.time && (!bs->currentEnemy || !bs->frame_Enemy_Vis))
	{
		return;
	}
	else if (bs->doChat && bs->currentEnemy && bs->frame_Enemy_Vis)
	{
		//bs->chatTime = level.time + bs->chatTime_stored;
		bs->doChat = 0; //do we want to keep the bot waiting to chat until after the enemy is gone?
		bs->chatTeam = 0;
	}
	else if (bs->doChat && bs->chatTime <= level.time)
	{
		if (bs->chatTeam)
		{
			trap->EA_SayTeam(bs->client, bs->currentChat);
			bs->chatTeam = 0;
		}
		else
		{
			trap->EA_Say(bs->client, bs->currentChat);
		}
		if (bs->doChat == 2)
		{
			BotReplyGreetings(bs);
		}
		bs->doChat = 0;
	}

	CTFFlagMovement(bs);

	if (/*bs->wpDestination &&*/ bs->shootGoal &&
		/*bs->wpDestination->associated_entity == bs->shootGoal->s.number &&*/
		bs->shootGoal->health > 0 && bs->shootGoal->takedamage)
	{
		dif[0] = (bs->shootGoal->r.absmax[0] + bs->shootGoal->r.absmin[0]) / 2;
		dif[1] = (bs->shootGoal->r.absmax[1] + bs->shootGoal->r.absmin[1]) / 2;
		dif[2] = (bs->shootGoal->r.absmax[2] + bs->shootGoal->r.absmin[2]) / 2;

		if (!bs->currentEnemy || bs->frame_Enemy_Len > 256)
		{ //if someone is close then don't stop shooting them for this
			VectorSubtract(dif, bs->eye, a);
			vectoangles(a, a);
			VectorCopy(a, bs->goalAngles);

			if (InFieldOfVision(bs->viewangles, 30, a) &&
				EntityVisibleBox(bs->origin, NULL, NULL, dif, bs->client, bs->shootGoal->s.number))
			{
				bs->doAttack = 1;
			}
		}
	}

	if (bs->cur_ps.hasDetPackPlanted)
	{ //check if our enemy gets near it and detonate if he does
		BotCheckDetPacks(bs);
	}
	else if (bs->currentEnemy && bs->lastVisibleEnemyIndex == bs->currentEnemy->s.number && !bs->frame_Enemy_Vis && bs->plantTime < level.time &&
		!bs->doAttack && !bs->doAltAttack)
	{
		VectorSubtract(bs->origin, bs->hereWhenSpotted, a);

		if (bs->plantDecided > level.time || (bs->frame_Enemy_Len < BOT_PLANT_DISTANCE * 2 && VectorLength(a) < BOT_PLANT_DISTANCE))
		{
			mineSelect = BotSelectChoiceWeapon(bs, WP_TRIP_MINE, 0);
			detSelect = BotSelectChoiceWeapon(bs, WP_DET_PACK, 0);
			if (bs->cur_ps.hasDetPackPlanted)
			{
				detSelect = 0;
			}

			if (bs->plantDecided > level.time && bs->forceWeaponSelect &&
				bs->cur_ps.weapon == bs->forceWeaponSelect)
			{
				bs->doAttack = 1;
				bs->plantDecided = 0;
				bs->plantTime = level.time + BOT_PLANT_INTERVAL;
				bs->plantContinue = level.time + 500;
				bs->beStill = level.time + 500;
			}
			else if (mineSelect || detSelect)
			{
				if (BotSurfaceNear(bs))
				{
					if (!mineSelect)
					{ //if no mines use detpacks, otherwise use mines
						mineSelect = WP_DET_PACK;
					}
					else
					{
						mineSelect = WP_TRIP_MINE;
					}

					detSelect = BotSelectChoiceWeapon(bs, mineSelect, 1);

					if (detSelect && detSelect != 2)
					{ //We have it and it is now our weapon
						bs->plantDecided = level.time + 1000;
						bs->forceWeaponSelect = mineSelect;
						return;
					}
					else if (detSelect == 2)
					{
						bs->forceWeaponSelect = mineSelect;
						return;
					}
				}
			}
		}
	}
	else if (bs->plantContinue < level.time)
	{
		bs->forceWeaponSelect = 0;
	}

	if (level.gametype == GT_JEDIMASTER && !bs->cur_ps.isJediMaster && bs->jmState == -1 && gJMSaberEnt && gJMSaberEnt->inuse)
	{
		vec3_t saberLen;
		float fSaberLen = 0;

		VectorSubtract(bs->origin, gJMSaberEnt->r.currentOrigin, saberLen);
		fSaberLen = VectorLength(saberLen);

		if (fSaberLen < 256)
		{
			if (OrgVisible(bs->origin, gJMSaberEnt->r.currentOrigin, bs->client))
			{
				VectorCopy(gJMSaberEnt->r.currentOrigin, bs->goalPosition);
			}
		}
	}

	if (bs->beStill < level.time && !WaitingForNow(bs, bs->goalPosition) && !fjHalt)
	{
		// CHECK FOR WALLS BEFORE MOVEMENT
		vec3_t mins, maxs, forward, end;
		trace_t tr;
		float wallDistance;

		VectorSet(mins, -15, -15, -8);
		VectorSet(maxs, 15, 15, 32);

		AngleVectors(bs->viewangles, forward, NULL, NULL);
		VectorMA(bs->origin, 32, forward, end);
		JP_Trace(&tr, bs->origin, mins, maxs, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);
		wallDistance = tr.fraction * 32;

		if (wallDistance < 16) // Niksata Edit
		{
			// SHIP WING BACKUP FIX - Don't back up if we've been backing up recently
			static int lastBackupTime = 0;
			static int consecutiveBackups = 0;

			if (level.time - lastBackupTime < 2000) // If we haven't backed up recently
			{
				consecutiveBackups = 0; // Reset counter
			}
			else
			{
				consecutiveBackups++; // Increment consecutive backups
			}

			// PREVENT INFINITE BACKUPS - Max 3 consecutive backups
			if (consecutiveBackups >= 3)
			{
				// Try alternative: jump and turn 90 degrees
				if (bs->jumpTime < level.time)
				{
					bs->jumpTime = level.time + 500;
					trap->EA_Jump(bs->client);
				}

				// Turn 90 degrees and try forward
				bs->ideal_viewangles[YAW] += 90;
				MoveTowardIdealAngles(bs);

				consecutiveBackups = 0; // Reset after trying alternative
				lastBackupTime = level.time + 3000; // Cooldown period
				return; // Skip normal backup logic
			}

			// Normal backup logic (reduced frequency)
			if (Q_irand(1, 10) <= 6) // 70% chance to strafe
			{
				if (Q_irand(1, 2) == 1)
					trap->EA_MoveRight(bs->client);
				else
					trap->EA_MoveLeft(bs->client);
			}
			else // 30% chance to backup
			{
				trap->EA_MoveBack(bs->client);
				bs->ideal_viewangles[YAW] += Q_irand(-45, 45);
			}

			lastBackupTime = level.time;
			bs->beStill = level.time + 300;
			goto skip_movement;
		} // Niksata Edit

		// ENHANCED STUCK DETECTION AND RECOVERY // Niksata Edit

		if (level.time - lastStuckCheckTime > 1000) // Check every second
		{
			float movedDistance = Distance(bs->origin, lastStuckCheckPos);

			if (movedDistance < 16) // Reduced threshold for better detection
			{
				stuckCounter++;

				// If stuck for 3+ seconds, try aggressive recovery
				if (stuckCounter >= 3)
				{
					// AGGRESSIVE UNSTUCK MANEUVERS
					if (bs->jumpTime < level.time)
					{
						bs->jumpTime = level.time + 500;
						trap->EA_Jump(bs->client);

						// Random direction to escape
						int randomDir = Q_irand(1, 8);
						switch (randomDir)
						{
						case 1: case 2:
							trap->EA_MoveForward(bs->client);
							break;
						case 3: case 4:
							trap->EA_MoveRight(bs->client);
							break;
						case 5: case 6:
							trap->EA_MoveBack(bs->client);
							break;
						case 7: case 8:
							trap->EA_MoveLeft(bs->client);
							break;
						}
					}

					stuckCounter = 0;
					lastStuckCheckTime = level.time + 2000;
				}
				else if (stuckCounter >= 2)
				{
					// MODERATE RECOVERY - Try 180-degree turn
					bs->ideal_viewangles[YAW] += 180;
					MoveTowardIdealAngles(bs);

					if (bs->jumpTime < level.time)
					{
						bs->jumpTime = level.time + 300;
						trap->EA_Jump(bs->client);
					}
				}
			}
			else
			{
				stuckCounter = 0;
			}

			VectorCopy(bs->origin, lastStuckCheckPos);
			lastStuckCheckTime = level.time;
		} // Niksata Edit

		VectorSubtract(bs->goalPosition, bs->origin, bs->goalMovedir);
		VectorNormalize(bs->goalMovedir);

		if (bs->jumpTime > level.time && bs->jDelay < level.time &&
			level.clients[bs->client].pers.cmd.upmove > 0)
		{
			bs->beStill = level.time + 200;
		}
		else
		{
			trap->EA_Move(bs->client, bs->goalMovedir, 5000);
		}

		// PRIORITY: Attacks override movement
		 // ENHANCED MOVEMENT SYSTEM // Niksata Edit
		if (bs->doAttack || bs->doAltAttack) {
			// Allow movement during attack windup
			BotMovementDuringAttack(bs);

			// Only skip movement during actual swing
			if (bs->attackRecoveryTime > level.time) {
				goto skip_movement;
			}
		}

		// Dynamic combat movement
		BotDynamicCombatMovement(bs); // Niksata Edit

		// REDUCED STRAFE FREQUENCY - Only 40% chance
		if (meleestrafe && bs->currentEnemy && bs->frame_Enemy_Vis)
		{
			// Only strafe if we're not actively attacking and enemy is in saber range
			if (bs->frame_Enemy_Len <= SABER_ATTACK_RANGE &&
				!bs->doAttack && !bs->doAltAttack &&
				(bs->meleeStrafeDisable < level.time))
			{
				// Reduced strafe frequency - only 40% chance per frame
				if (Q_irand(1, 10) <= 4)
				{
					if (bs->meleeStrafeDir)
					{
						trap->EA_MoveRight(bs->client);
					}
					else
					{
						trap->EA_MoveLeft(bs->client);
					}
				}
			}
		}

		if (BotTrace_Jump(bs, bs->goalPosition))
		{
			bs->jumpTime = level.time + 100;
		}
		else if (BotTrace_Duck(bs, bs->goalPosition))
		{
			// SIMPLE DUCKING FIX - Don't duck if we have an enemy nearby
			if (!bs->currentEnemy || bs->frame_Enemy_Len > 200)
			{
				// Only 10% chance to duck, and not if we were just ducking
				if (bs->duckTime < level.time - 500 && Q_irand(1, 10) <= 1)
				{
					bs->duckTime = level.time + 100;
				}
			}
		} // Niksata Edit
#ifdef BOT_STRAFE_AVOIDANCE
		else
		{
			int strafeAround = BotTrace_Strafe(bs, bs->goalPosition);

			if (strafeAround == STRAFEAROUND_RIGHT)
			{
				trap->EA_MoveRight(bs->client);
			}
			else if (strafeAround == STRAFEAROUND_LEFT)
			{
				trap->EA_MoveLeft(bs->client);
			}
		}
#endif
	}

#ifndef FORCEJUMP_INSTANTMETHOD
	if (bs->forceJumpChargeTime > level.time)
	{
		bs->jumpTime = 0;
	}
#endif

skip_movement: // Niksata Edit

	if (bs->jumpPrep > level.time)
	{
		bs->forceJumpChargeTime = 0;
	}

	if (bs->forceJumpChargeTime > level.time)
	{
		bs->jumpHoldTime = ((bs->forceJumpChargeTime - level.time) / 2) + level.time;
		bs->forceJumpChargeTime = 0;
	}

	if (bs->jumpHoldTime > level.time)
	{
		bs->jumpTime = bs->jumpHoldTime;
	}

	if (bs->jumpTime > level.time && bs->jDelay < level.time)
	{
		if (bs->jumpHoldTime > level.time)
		{
			trap->EA_Jump(bs->client);
			if (bs->wpCurrent)
			{
				if ((bs->wpCurrent->origin[2] - bs->origin[2]) < 64)
				{
					trap->EA_MoveForward(bs->client);
				}
			}
			else
			{
				trap->EA_MoveForward(bs->client);
			}
			if (g_entities[bs->client].client->ps.groundEntityNum == ENTITYNUM_NONE)
			{
				g_entities[bs->client].client->ps.pm_flags |= PMF_JUMP_HELD;
			}
		}
		else if (!(bs->cur_ps.pm_flags & PMF_JUMP_HELD))
		{
			trap->EA_Jump(bs->client);
		}
	}

	if (bs->duckTime > level.time)
	{
		trap->EA_Crouch(bs->client);
	}

	if (bs->dangerousObject && bs->dangerousObject->inuse && bs->dangerousObject->health > 0 &&
		bs->dangerousObject->takedamage && (!bs->frame_Enemy_Vis || !bs->currentEnemy) &&
		(BotGetWeaponRange(bs) == BWEAPONRANGE_MID || BotGetWeaponRange(bs) == BWEAPONRANGE_LONG) &&
		bs->cur_ps.weapon != WP_DET_PACK && bs->cur_ps.weapon != WP_TRIP_MINE &&
		!bs->shootGoal)
	{
		float danLen;

		VectorSubtract(bs->dangerousObject->r.currentOrigin, bs->eye, a);

		danLen = VectorLength(a);

		if (danLen > 256)
		{
			vectoangles(a, a);
			VectorCopy(a, bs->goalAngles);

			if (Q_irand(1, 10) < 5)
			{
				bs->goalAngles[YAW] += Q_irand(0, 3);
				bs->goalAngles[PITCH] += Q_irand(0, 3);
			}
			else
			{
				bs->goalAngles[YAW] -= Q_irand(0, 3);
				bs->goalAngles[PITCH] -= Q_irand(0, 3);
			}

			if (InFieldOfVision(bs->viewangles, 30, a) &&
				EntityVisibleBox(bs->origin, NULL, NULL, bs->dangerousObject->r.currentOrigin, bs->client, bs->dangerousObject->s.number))
			{
				bs->doAttack = 1;
			}
		}
	}

	if (PrimFiring(bs) ||
		AltFiring(bs))
	{
		friendInLOF = CheckForFriendInLOF(bs);

		if (friendInLOF)
		{
			if (PrimFiring(bs))
			{
				KeepPrimFromFiring(bs);
			}
			if (AltFiring(bs))
			{
				KeepAltFromFiring(bs);
			}
			if (useTheForce && forceHostile)
			{
				useTheForce = 0;
			}

			if (!useTheForce && friendInLOF->client)
			{ //we have a friend here and are not currently using force powers, see if we can help them out
				if (friendInLOF->health <= 50 && level.clients[bs->client].ps.fd.forcePower > forcePowerNeeded[level.clients[bs->client].ps.fd.forcePowerLevel[FP_TEAM_HEAL]][FP_TEAM_HEAL])
				{
					level.clients[bs->client].ps.fd.forcePowerSelected = FP_TEAM_HEAL;
					useTheForce = 1;
					forceHostile = 0;
				}
				else if (friendInLOF->client->ps.fd.forcePower <= 50 && level.clients[bs->client].ps.fd.forcePower > forcePowerNeeded[level.clients[bs->client].ps.fd.forcePowerLevel[FP_TEAM_FORCE]][FP_TEAM_FORCE])
				{
					level.clients[bs->client].ps.fd.forcePowerSelected = FP_TEAM_FORCE;
					useTheForce = 1;
					forceHostile = 0;
				}
			}
		}
	}
	else if (level.gametype >= GT_TEAM)
	{ //still check for anyone to help..
		friendInLOF = CheckForFriendInLOF(bs);

		if (!useTheForce && friendInLOF)
		{
			if (friendInLOF->health <= 50 && level.clients[bs->client].ps.fd.forcePower > forcePowerNeeded[level.clients[bs->client].ps.fd.forcePowerLevel[FP_TEAM_HEAL]][FP_TEAM_HEAL])
			{
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_TEAM_HEAL;
				useTheForce = 1;
				forceHostile = 0;
			}
			else if (friendInLOF->client->ps.fd.forcePower <= 50 && level.clients[bs->client].ps.fd.forcePower > forcePowerNeeded[level.clients[bs->client].ps.fd.forcePowerLevel[FP_TEAM_FORCE]][FP_TEAM_FORCE])
			{
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_TEAM_FORCE;
				useTheForce = 1;
				forceHostile = 0;
			}
		}
	}

	if (bs->doAttack && bs->cur_ps.weapon == WP_DET_PACK &&
		bs->cur_ps.hasDetPackPlanted)
	{ //maybe a bit hackish, but bots only want to plant one of these at any given time to avoid complications
		bs->doAttack = 0;
	}

	if (bs->doAttack && bs->cur_ps.weapon == WP_SABER &&
		bs->saberDefending && bs->currentEnemy && bs->currentEnemy->client &&
		BotWeaponBlockable(bs->currentEnemy->client->ps.weapon))
	{
		bs->doAttack = 0;
	}

	if (bs->cur_ps.saberLockTime > level.time)
	{
		if (rand() % 10 < 5)
		{
			bs->doAttack = 1;
		}
		else
		{
			bs->doAttack = 0;
		}
	}

	if (bs->botChallengingTime > level.time)
	{
		bs->doAttack = 0;
		bs->doAltAttack = 0;
	}

	if (bs->cur_ps.weapon == WP_SABER &&
		bs->cur_ps.saberInFlight &&
		!bs->cur_ps.saberEntityNum)
	{ //saber knocked away, keep trying to get it back
		bs->doAttack = 1;
		bs->doAltAttack = 0;
	}

	if (bs->doAttack)
	{
		trap->EA_Attack(bs->client);
	}
	else if (bs->doAltAttack)
	{
		trap->EA_Alt_Attack(bs->client);
	}

	if (useTheForce && forceHostile && bs->botChallengingTime > level.time)
	{
		useTheForce = qfalse;
	}

	if (useTheForce)
	{
#ifndef FORCEJUMP_INSTANTMETHOD
		if (bs->forceJumpChargeTime > level.time)
		{
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_LEVITATION;
			trap->EA_ForcePower(bs->client);
		}
		else
		{
#endif
			if (bot_forcepowers.integer && !g_forcePowerDisable.integer)
			{
				trap->EA_ForcePower(bs->client);
			}
#ifndef FORCEJUMP_INSTANTMETHOD
		}
#endif
	}

	MoveTowardIdealAngles(bs);
}

int gUpdateVars = 0;

/*
==================
BotAIStartFrame
==================
*/
int BotAIStartFrame(int time) {
	int i;

	// ADD TIMEOUT PROTECTION:
	static int last_frame_time = 0;
	int current_time = trap->Milliseconds();

	if (current_time - last_frame_time > 100) {  // 100ms timeout
		BotAI_Print(PRT_WARNING, "BotAIStartFrame: Frame timeout\n");
		last_frame_time = current_time;
		return qtrue;  // Continue but skip this frame
	}

	for (i = 0; i < MAX_CLIENTS; i++) {
		// BOUNDS CHECK:
		if (i < 0 || i >= MAX_CLIENTS) {
			continue;
		}

		if (!botstates[i] || !botstates[i]->inuse) {
			continue;
		}

		// ENTITY VALIDATION:
		if (!g_entities[i].inuse || !g_entities[i].client) {
			continue;
		}

		if (g_entities[i].client->pers.connected != CON_CONNECTED) {
			continue;
		}
	}

	int elapsed_time, thinktime;
	static int local_time;
	//	static int botlib_residual;
	static int lastbotthink_time;

	if (gUpdateVars < level.time)
	{
		trap->Cvar_Update(&bot_pvstype);
		trap->Cvar_Update(&bot_camp);
		trap->Cvar_Update(&bot_attachments);
		trap->Cvar_Update(&bot_forgimmick);
		trap->Cvar_Update(&bot_honorableduelacceptance);
#ifndef FINAL_BUILD
		trap->Cvar_Update(&bot_getinthecarrr);
#endif
		gUpdateVars = level.time + 1000;
	}

	G_CheckBotSpawn();

	//rww - addl bot frame functions
	if (gBotEdit)
	{
		trap->Cvar_Update(&bot_wp_info);
		BotWaypointRender();
	}

	UpdateEventTracker();
	//end rww

	//cap the bot think time
	//if the bot think time changed we should reschedule the bots
	if (BOT_THINK_TIME != lastbotthink_time) {
		lastbotthink_time = BOT_THINK_TIME;
		BotScheduleBotThink();
	}

	elapsed_time = time - local_time;
	local_time = time;

	if (elapsed_time > BOT_THINK_TIME) thinktime = elapsed_time;
	else thinktime = BOT_THINK_TIME;

	// execute scheduled bot AI
	for (i = 0; i < MAX_CLIENTS; i++) {
		if (!botstates[i] || !botstates[i]->inuse) {
			continue;
		}
		//
		botstates[i]->botthink_residual += elapsed_time;
		//
		if (botstates[i]->botthink_residual >= thinktime) {
			botstates[i]->botthink_residual -= thinktime;

			if (g_entities[i].client->pers.connected == CON_CONNECTED) {
				BotAI(i, (float)thinktime / 1000);
			}
		}
	}

	// execute bot user commands every frame
	for (i = 0; i < MAX_CLIENTS; i++) {
		if (!botstates[i] || !botstates[i]->inuse) {
			continue;
		}
		if (g_entities[i].client->pers.connected != CON_CONNECTED) {
			continue;
		}

		BotUpdateInput(botstates[i], time, elapsed_time);
		trap->BotUserCommand(botstates[i]->client, &botstates[i]->lastucmd);
	}

	return qtrue;
}

/*
==============
BotAISetup
==============
*/
int BotAISetup(int restart) {
	//rww - new bot cvars..
	trap->Cvar_Register(&bot_forcepowers, "bot_forcepowers", "1", CVAR_CHEAT);
	trap->Cvar_Register(&bot_forgimmick, "bot_forgimmick", "0", CVAR_CHEAT);
	trap->Cvar_Register(&bot_honorableduelacceptance, "bot_honorableduelacceptance", "0", CVAR_ARCHIVE);
	trap->Cvar_Register(&bot_pvstype, "bot_pvstype", "1", CVAR_CHEAT);
#ifndef FINAL_BUILD
	trap->Cvar_Register(&bot_getinthecarrr, "bot_getinthecarrr", "0", 0);
#endif

#ifdef _DEBUG
	trap->Cvar_Register(&bot_nogoals, "bot_nogoals", "0", CVAR_CHEAT);
	trap->Cvar_Register(&bot_debugmessages, "bot_debugmessages", "0", CVAR_CHEAT);
#endif

	trap->Cvar_Register(&bot_attachments, "bot_attachments", "1", 0);
	trap->Cvar_Register(&bot_camp, "bot_camp", "1", 0);

	trap->Cvar_Register(&bot_wp_info, "bot_wp_info", "1", 0);
	trap->Cvar_Register(&bot_wp_edit, "bot_wp_edit", "0", CVAR_CHEAT);
	trap->Cvar_Register(&bot_wp_clearweight, "bot_wp_clearweight", "1", 0);
	trap->Cvar_Register(&bot_wp_distconnect, "bot_wp_distconnect", "1", 0);
	trap->Cvar_Register(&bot_wp_visconnect, "bot_wp_visconnect", "1", 0);

	trap->Cvar_Update(&bot_forcepowers);
	//end rww

	//if the game is restarted for a tournament
	if (restart) {
		return qtrue;
	}

	//initialize the bot states
	memset(botstates, 0, sizeof(botstates));

	if (!trap->BotLibSetup())
	{
		return qfalse; //wts?!
	}

	return qtrue;
}

/*
==============
BotAIShutdown
==============
*/
int BotAIShutdown(int restart) {

	int i;

	//if the game is restarted for a tournament
	if (restart) {
		//shutdown all the bots in the botlib
		for (i = 0; i < MAX_CLIENTS; i++) {
			if (botstates[i] && botstates[i]->inuse) {
				BotAIShutdownClient(botstates[i]->client, restart);
			}
		}
		//don't shutdown the bot library
	}
	else {
		trap->BotLibShutdown();
	}
	return qtrue;
}

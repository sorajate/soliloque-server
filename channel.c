/*
 * soliloque-server, an open source implementation of the TeamSpeak protocol.
 * Copyright (C) 2009 Hugo Camboulive <hugo.camboulive AT gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "channel.h"
#include "array.h"
#include "log.h"
#include "database.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>

/**
 * Destroys a channel and all its fields
 *
 * @param chan the channel we are going to destroy
 *
 * @return 1 on success
 */
int destroy_channel(struct channel *chan)
{
	size_t iter;
	void *el;

	free(chan->name);
	free(chan->topic);
	free(chan->desc);
	ar_free(chan->players);

	/* destroy privileges */
	ar_each(void *, el, iter, chan->pl_privileges)
		ar_remove(chan->pl_privileges, el);
		destroy_player_channel_privilege(el);
	ar_end_each;
	ar_free(chan->pl_privileges);
	/* destroy subchannels */
	ar_each(void *, el, iter, chan->subchannels)
		ar_remove(chan->subchannels, el);
	ar_end_each;
	ar_free(chan->subchannels);

	free(chan);
	return 1;
}

/**
 * Initialize a new channel.
 * 
 * @param name the name of the channel
 * @param topic the topic of the channel
 * @param desc the description of the channel
 * @param flags the flags of the channel in a bitfield (see channel.h for values)
 * @param codec the codec of the channel (see channel.h for values)
 * @param sort_order the sort order of the channel. No idea what it is
 * @param max_users the maximum number of users in the channel
 *
 * @return the newly allocated and initialized channel
 */
struct channel *new_channel(char *name, char *topic, char *desc, uint16_t flags,
		uint16_t codec, uint16_t sort_order, uint16_t max_users)
{
	struct channel *chan;
	chan = (struct channel *)calloc(1, sizeof(struct channel));
	if (chan == NULL) {
		logger(LOG_ERR, "new_channel, calloc failed : %s.", strerror(errno));
		return NULL;
	}
	
	bzero(chan->password, 30);
	chan->players = ar_new(4);
	chan->players->max_slots = max_users;
	/* subchannels */
	chan->subchannels = ar_new(4);
	chan->parent = NULL;
	/* player privileges */
	chan->pl_privileges = ar_new(4);

	/* strdup : input strings are secure */
	chan->name = strdup(name);
	chan->topic = strdup(topic);
	chan->desc = strdup(desc);

	chan->flags = flags;
	chan->codec = codec;
	chan->sort_order = sort_order;

	if (chan->name == NULL || chan->topic == NULL || chan->desc == NULL) {
		if (chan->name != NULL)
			free(chan->name);
		if (chan->topic != NULL)
			free(chan->topic);
		if (chan->desc != NULL)
			free(chan->desc);
		free(chan);
		return NULL;
	}
	
	return chan;
}

/**
 * Initialize a default channel for testing.
 * This will not give a channel it's ID (determined at insertion)
 *
 * @return a test channel.
 */
struct channel *new_predef_channel()
{
	uint16_t flags = (0 & ~CHANNEL_FLAG_UNREGISTERED);
	return new_channel("Channel name", "Channel topic", "Channel description", flags, CODEC_SPEEX_19_6, 0, 16);
}


/**
 * Prints a channel's information.
 * @param chan a channel
 */
void print_channel(struct channel *chan)
{
	if (chan == NULL) {
		logger(LOG_INFO, "Channel NULL");
	} else {
		logger(LOG_INFO, "Channel ID %i", chan->id);
		logger(LOG_INFO, "\t name    : %s", chan->name);
		logger(LOG_INFO, "\t topic   : %s", chan->topic);
		logger(LOG_INFO, "\t desc    : %s", chan->desc);
		if(ch_getflags(chan) & CHANNEL_FLAG_DEFAULT)
			logger(LOG_INFO, "\t default : y");
	}
}

/**
 * Add a player to a channel (both have to exist).
 * This will NOT remove the player from his existing channel if
 * he has one.
 * @param chan an existing channel
 * @param pl an existing player
 *
 * @return 1 if the insertion succeeded, 
 * 	   0 if it failed (maximum number of users in channel already reached)
 */
int add_player_to_channel(struct channel *chan, struct player *pl)
{
	if (ch_isfull(chan))
		return 0;

	if (ar_insert(chan->players, pl) == AR_OK) {
		pl->in_chan = chan;
		return 1;
	}
	return 0;
}

/**
 * Converts a channel to a data block that can be sent
 * over the network.
 *
 * @param ch the channel we are going to convert to a data block
 * @param data a pointer to an already allocated data block
 *
 * @return the number of bytes that have been written
 */
int channel_to_data(struct channel *ch, char *data)
{
	int size = 4 + 2 + 2 + 4 + 2 + 2
	        + strlen(ch->name) + 1
		+ strlen(ch->topic) + 1
		+ strlen(ch->desc) + 1;
	char *ptr;

	ptr = data;

	wu32(ch->id, &ptr);
	wu16(ch->flags, &ptr);
	wu16(ch->codec, &ptr);
	if (ch->parent == NULL) {
		wu32(0xFFFFFFFF, &ptr);
	} else {
		wu32(ch->parent->id, &ptr);
	}
	wu16(ch->sort_order, &ptr);
	wu16(ch->players->max_slots, &ptr);
	strcpy(ptr, ch->name);			ptr += (strlen(ch->name) +1);
	strcpy(ptr, ch->topic);			ptr += (strlen(ch->topic) +1);
	strcpy(ptr, ch->desc);			ptr += (strlen(ch->desc) +1);

	assert((ptr - data) == size);

	return size;
}

/**
 * Compute the size the channel will take once converted
 * to raw data
 *
 * @param ch the channel
 *
 * @return the size the channel will take
 */
int channel_to_data_size(struct channel *ch)
{
	return 4 + 2 + 2 + 4 + 2 + 2
	        + strlen(ch->name) + 1
		+ strlen(ch->topic) + 1
		+ strlen(ch->desc) + 1;
}


size_t channel_from_data(char *data, int len, struct channel **dst)
{
	uint16_t flags;
	uint16_t codec;
	uint16_t sort_order;
	uint16_t max_users;
	uint32_t parent_id;
	char *name, *topic, *desc, *ptr;

	ptr = data + 4;
	flags = ru16(&ptr);
	codec = ru16(&ptr);
	parent_id = ru32(&ptr);
	sort_order = ru16(&ptr);
	max_users = ru16(&ptr);
	/* TODO : check if the len - (ptr - data) - X formula is correct */
	name = strndup(ptr, len - (ptr - data) - 3);		ptr += strlen(name) + 1;
	topic = strndup(ptr, len - (ptr - data) - 2);		ptr += strlen(topic) + 1;
	desc = strndup(ptr, len - (ptr - data) - 1);		ptr += strlen(desc) + 1;

	if (name == NULL || topic == NULL || desc == NULL) {
		if (name != NULL)
			free(name);
		if (topic != NULL)
			free(topic);
		if (desc != NULL)
			free(desc);
		logger(LOG_WARN, "channel_from_data, allocation failed : %s.", strerror(errno));
		return 0;
	}
	*dst = new_channel(name, topic, desc, flags, codec, sort_order, max_users);

	if (parent_id != 0xFFFFFFFF)
		(*dst)->parent_id = parent_id;

	return ptr - data;
}

int channel_remove_subchannel(struct channel *ch, struct channel *subchannel)
{
	if (ch != subchannel->parent) {
		logger(LOG_WARN, "channel_remove_subchannel, subchannel's parent is not the same as passed parent.");
		return 0;
	}
	if (ch == NULL) {
		logger(LOG_WARN, "channel_remove_subchannel, parent is null.");
		return 0;
	}
	ar_remove(ch->subchannels, subchannel);
	subchannel->parent = NULL;

	return 1;
}

int channel_add_subchannel(struct channel *ch, struct channel *subchannel)
{
	if ((ch_getflags(ch) & CHANNEL_FLAG_SUBCHANNELS) == 0) {
		logger(LOG_WARN, "channel_add_subchannel, channel %i:%s can not have subchannels.", ch->id, ch->name);
		return 0;
	}
	if (subchannel->parent != NULL)
		channel_remove_subchannel(subchannel->parent, subchannel);
	subchannel->parent = ch;
	ar_insert(ch->subchannels, subchannel);
	return 1;
}

/**
 * Return a channel flags if it is a top channel, or
 * the parent's flag if it is a subchannel.
 *
 * @param ch the channel we want the flags of
 *
 * @return the channel flags
 */
int ch_getflags(struct channel *ch)
{
	int flags;

	if (ch->parent == NULL)
		return ch->flags;

	flags = ch->parent->flags;
	/* A subchannel can not have subchannels and
	 * cannot be default. Remove those flags. */
	flags &= ~CHANNEL_FLAG_SUBCHANNELS;
	flags &= ~CHANNEL_FLAG_DEFAULT;
	
	return flags;
}

/**
 * Return the password of the channel (top channel),
 * the password of the parent channel (subchannel), or
 * NULL.
 *
 * @param ch the channel we want the password of.
 *
 * @return the password (SHA256-hashed) or NULL if the channel is
 * not password protected.
 */
char *ch_getpass(struct channel *ch)
{
	if (ch->parent != NULL)
		return ch_getpass(ch->parent);

	if (ch->flags & CHANNEL_FLAG_PASSWORD) {
		return ch->password;
	} else {
		logger(LOG_WARN, "channel_getpassword, asked for the password of a channel that is not protected.");
		return NULL;
	}
}

/**
 * Return wether a channel is full or not
 *
 * @param ch the channel
 *
 * @return 0 if the channel is not full, 1 if it is
 */
char ch_isfull(struct channel *ch)
{
	/* Max number of players in default channel is maximum
	 * number of element in the array */
	if (ch->flags & CHANNEL_FLAG_DEFAULT)
		return ch->players->used_slots == (size_t) -1;
	/* Else it is specified */
	return ch->players->used_slots >= ch->players->max_slots;
}

/**
 * Retrieve a player channel privileges for a given channel.
 *
 * @param pl the player we want the privileges for
 * @param ch the channel
 *
 * @return the privilege structure
 */
struct player_channel_privilege *get_player_channel_privilege(struct player *pl, struct channel *ch)
{
	struct channel *tmp_ch;
	struct player_channel_privilege *tmp_priv;
	size_t iter;

	tmp_ch = ch;
	if (ch->parent != NULL)
		tmp_ch = ch->parent;

	ar_each(struct player_channel_privilege *, tmp_priv, iter, tmp_ch->pl_privileges)
		if (tmp_priv->reg == PL_CH_PRIV_REGISTERED && tmp_priv->pl_or_reg.reg == pl->reg)
			return tmp_priv;
		if (tmp_priv->reg == PL_CH_PRIV_UNREGISTERED && tmp_priv->pl_or_reg.pl == pl)
			return tmp_priv;
	ar_end_each;

	logger(LOG_INFO, "Could not find privileges for this channel-player couple... Creating a new one");
	/* if there is no existing privileges, we create them */
	tmp_priv = new_player_channel_privilege();
	tmp_priv->ch = tmp_ch;
	if (pl->global_flags & GLOBAL_FLAG_REGISTERED) {
		tmp_priv->reg = PL_CH_PRIV_REGISTERED;
		tmp_priv->pl_or_reg.reg = pl->reg;
		/* register the privilege in the DB */
		if (!(tmp_ch->flags & CHANNEL_FLAG_UNREGISTERED)) {
			logger(LOG_INFO, "get_player_channel_privilege : adding a new priv to the DB");
			db_add_pl_chan_priv(tmp_ch->in_server->conf, tmp_priv);
		}
	} else {
		tmp_priv->reg = PL_CH_PRIV_UNREGISTERED;
		tmp_priv->pl_or_reg.pl = pl;
	}
	add_player_channel_privilege(tmp_ch, tmp_priv);

	return tmp_priv;
}

void add_player_channel_privilege(struct channel *ch, struct player_channel_privilege *priv)
{
	ar_insert(ch->pl_privileges, priv);
}

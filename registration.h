#ifndef __REGISTRATION_H__
#define __REGISTRATION_H__

struct registration
{
	char global_flags;	/* only serveradmin 0/1 */
	char name[30];
	char password[30];
};

struct registration *new_registration(void);

#endif

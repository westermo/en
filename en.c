#include <err.h>
#include <stdio.h>
#include <stdlib.h>

#include "clio.h"

void ip_show(ArgParser *ap)
{
	char cmd[256] = "ifconfig";

	if (ap_has_args(ap))
		snprintf(cmd, sizeof(cmd), "ifconfig %s", ap_get_arg(ap, 0));

	system(cmd);
}

int ip_init(ArgParser *ap)
{
	ArgParser *ip;

	ip = ap_add_cmd(ap, "show", "Command!", ip_show);
	if (!ip)
		return 1;

	return 0;
}

int main(int argc, char *argv[])
{
	ArgParser *ap;

	ap = ap_new("Help!", "Version!");
	if (!ap)
		err(1, "Someone set up us the bomb");

	if (ip_init(ap))
		err(1, "Failed ip init");

	ap_parse(ap, argc, argv);
	if (!ap_has_cmd(ap))
		errx(1, "Missing cmd");
	ap_free(ap);

	return 0;
}

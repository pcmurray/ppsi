/* mqx_port.h - utilities to support running under MQX
 * Copyright (c) 2016 by Bitronics, LLC
 * released into public domain
 */

//#include <stdio.h>	/* most will need redifinition here */
#include <stdarg.h>

int _io_vprintf (char *fmt, va_list args);

int printf (char *fmt, ...)
	{
	va_list args;
	va_start(args, fmt);

	return (_io_vprintf(fmt, args));
	}

int strcmp (char *s1, char *s2);

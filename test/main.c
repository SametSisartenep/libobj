#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include "../obj.h"

static char fd0[] = "/fd/0";

void
replacetexfileexts(OBJMaterlist *ml, char *newext)
{
	OBJMaterial *m;
	char *s;
	int i;

	for(i = 0; i < nelem(ml->mattab); i++)
		for(m = ml->mattab[i]; m != nil; m = m->next){
			if(m->map_Kd != nil){
				s = strrchr(m->map_Kd->filename, '.');
				if(s != nil && strlen(newext) <= strlen(++s))
					memmove(s, newext, strlen(newext));
			}
			if(m->map_Ks != nil){
				s = strrchr(m->map_Ks->filename, '.');
				if(s != nil && strlen(newext) <= strlen(++s))
					memmove(s, newext, strlen(newext));
			}
			if(m->norm != nil){
				s = strrchr(m->norm->filename, '.');
				if(s != nil && strlen(newext) <= strlen(++s))
					memmove(s, newext, strlen(newext));
			}
		}
}

void
usage(void)
{
	fprint(2, "usage: %s [file [dstdir]]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	OBJ *obj;
	char *f, *d;

	f = fd0;
	d = nil;
	OBJfmtinstall();
	ARGBEGIN{
	default: usage();
	}ARGEND;
	if(argc > 2)
		usage();
	if(argc == 1)
		f = argv[0];
	else if(argc == 2){
		f = argv[0];
		d = argv[1];
	}

	obj = objparse(f);
	if(obj == nil)
		sysfatal("objparse: %r");

//	replacetexfileexts(obj->materials, "png");

	if(d == nil){
		if(obj->materials != nil)
			print("%M\n", obj->materials);
		print("%O\n", obj);
	}else{
		if(objexport(d, obj) < 0)
			sysfatal("objexport: %r");
	}
	objfree(obj);
	exits(nil);
}

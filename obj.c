#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <bio.h>
#include <draw.h>
#include <memdraw.h>
#include <obj.h>

#undef isspace(c)
#define isspace(c)	((c) == ' ' || (c) == '\t')
#define isobjname(c)	(isalnum(c) || (c) == '.' || (c) == '_' || (c) == '-')
#define isapath(c)	(isobjname(c) || (c) == '/')
#define ismtlname(c)	(isobjname(c) || (c) == '(' || (c) == ')')

typedef struct Line Line;
struct Line
{
	char *file;
	ulong line;
};

static OBJVertex ZV;
static OBJColor ZC;

static void
error(Line *l, char *fmt, ...)
{
	va_list va;
	char buf[ERRMAX], *bp;

	va_start(va, fmt);
	bp = seprint(buf, buf + sizeof buf, "%s:%lud ", l->file, l->line);
	vseprint(bp, buf + sizeof buf, fmt, va);
	va_end(va);
	werrstr("%s", buf);
}

static void *
emalloc(ulong n)
{
	void *p;

	p = malloc(n);
	if(p == nil)
		sysfatal("malloc: %r");
	memset(p, 0, n);
	setmalloctag(p, getcallerpc(&n));
	return p;
}

static void *
erealloc(void *v, ulong n)
{
	void *nv;

	nv = realloc(v, n);
	if(nv == nil)
		sysfatal("realloc: %r");
	setrealloctag(nv, getcallerpc(&v));
	return nv;
}

static char *
estrdup(char *s)
{
	char *ns;

	ns = strdup(s);
	if(ns == nil)
		sysfatal("strdup: %r");
	return ns;
}

static int
max(int a, int b)
{
	return a > b? a: b;
}

static uint
hash(char *s)
{
	uint h;

	h = 0x811c9dc5;
	while(*s != 0)
		h = (h^(uchar)*s++) * 0x1000193;
	return h % OBJHTSIZE;
}

typedef struct Deco Deco;
struct Deco
{
	int pfd[2];
	int infd;
	char *prog;
};

static void
decproc(void *arg)
{
	char buf[32];
	Deco *d;

	d = arg;

	close(d->pfd[0]);
	dup(d->infd, 0);
	close(d->infd);
	dup(d->pfd[1], 1);
	close(d->pfd[1]);

	snprint(buf, sizeof buf, "/bin/%s", d->prog);

	execl(buf, d->prog, "-9t", nil);
	sysfatal("execl: %r");
}

static Memimage *
genreadimage(char *prog, char *path)
{
	Memimage *i;
	Deco d;

	d.prog = prog;

	if(pipe(d.pfd) < 0)
		sysfatal("pipe: %r");
	d.infd = open(path, OREAD);
	if(d.infd < 0)
		sysfatal("open: %r");
	switch(fork()){
	case -1:
		sysfatal("fork: %r");
	case 0:
		decproc(&d);
	default:
		close(d.pfd[1]);
		i = readmemimage(d.pfd[0]);
		close(d.pfd[0]);
		close(d.infd);
	}

	return i;
}

static Memimage *
readimagefile(char *path)
{
	char *ext;
	static struct {
		char *ext;
		char *prog;
	} fmttab[] = {
		"tga", "tga",
		"png", "png",
		"jpg", "jpg",
		"jpeg", "jpg",
	}, *fmt;

	ext = strrchr(path, '.');
	if(ext++ != nil){
		for(fmt = &fmttab[0]; fmt < fmttab+nelem(fmttab); fmt++)
			if(strcmp(ext, fmt->ext) == 0)
				return genreadimage(fmt->prog, path);
		werrstr("file format not supported");
	}else
		werrstr("unknown format");
	return nil;
}

static OBJTexture *
readtexturefile(char *path)
{
	OBJTexture *t;
	char *s;

	t = emalloc(sizeof *t);
	t->image = readimagefile(path);
	if(t->image == nil){
		werrstr("readimagefile: %r");
		return nil;
	}
	s = strrchr(path, '/');
	t->filename = s == nil? estrdup(path): estrdup(++s);
	return t;
}

typedef struct Enco Enco;
struct Enco
{
	int pfd[2];
	int outfd;
	char *prog;
};

static void
encproc(void *arg)
{
	char buf[32];
	Enco *d;

	d = arg;

	close(d->pfd[1]);
	dup(d->pfd[0], 0);
	close(d->pfd[0]);
	dup(d->outfd, 1);
	close(d->outfd);

	snprint(buf, sizeof buf, "/bin/%s", d->prog);

	execl(buf, d->prog, nil);
	sysfatal("execl: %r");
}

static int
genwriteimage(char *prog, char *path, Memimage *i)
{
	Enco d;
	int rc;

	d.prog = prog;

	if(pipe(d.pfd) < 0)
		sysfatal("pipe: %r");
	d.outfd = create(path, OWRITE|OEXCL, 0644);
	if(d.outfd < 0){
		werrstr("create: %r");
		return -1;
	}
	switch(fork()){
	case -1:
		sysfatal("fork: %r");
	case 0:
		encproc(&d);
	default:
		close(d.pfd[0]);
		rc = writememimage(d.pfd[1], i);
		close(d.pfd[1]);
		close(d.outfd);
	}

	return rc;
}

static int
writeimagefile(char *path, Memimage *i)
{
	char *ext;
	static struct {
		char *ext;
		char *prog;
	} fmttab[] = {
		"png", "topng",
		"jpg", "tojpg",
		"jpeg", "tojpg",
	}, *fmt;

	ext = strrchr(path, '.');
	if(ext++ != nil){
		for(fmt = &fmttab[0]; fmt < fmttab+nelem(fmttab); fmt++)
			if(strcmp(ext, fmt->ext) == 0)
				return genwriteimage(fmt->prog, path, i);
		werrstr("file format not supported");
	}else
		werrstr("unknown format");
	return -1;
}

static void
addvertva(OBJVertexArray *va, OBJVertex v)
{
	va->verts = erealloc(va->verts, ++va->nvert*sizeof(OBJVertex));
	va->verts[va->nvert-1] = v;
}

static void
addvert(OBJ *obj, OBJVertex v, int vtype)
{
	addvertva(&obj->vertdata[vtype], v);
}

static void
addelem(OBJObject *o, OBJElem *e)
{
	if(o->lastone == nil){
		o->lastone = o->child = e;
		return;
	}
	o->lastone->next = e;
	o->lastone = o->lastone->next;
}

static OBJElem *
allocelem(int t)
{
	OBJElem *e;

	e = emalloc(sizeof(OBJElem));
	e->type = t;
	e->mtl = nil;
	return e;
}

static void
addelemidx(OBJElem *e, int idxtab, int idx)
{
	OBJIndexArray *tab;

	tab = &e->indextab[idxtab];
	tab->indices = erealloc(tab->indices, ++tab->nindex*sizeof(int));
	tab->indices[tab->nindex-1] = idx;
}

static void
freeelem(OBJElem *e)
{
	int i;

	for(i = 0; i < nelem(e->indextab); i++)
		free(e->indextab[i].indices);
	free(e);
}

static OBJObject *
alloco(char *n)
{
	OBJObject *o;

	o = emalloc(sizeof(OBJObject));
	o->name = estrdup(n);
	return o;
}

static void
freeo(OBJObject *o)
{
	OBJElem *e, *ne;

	free(o->name);
	for(e = o->child; e != nil; e = ne){
		ne = e->next;
		freeelem(e);
	}
	free(o);
}

static void
pusho(OBJ *obj, OBJObject *o)
{
	OBJObject *op, *prev;
	uint h;

	prev = nil;
	h = hash(o->name);
	for(op = obj->objtab[h]; op != nil; prev = op, op = op->next)
		if(strcmp(op->name, o->name) == 0){
			o->next = op->next;
			freeo(op);
			break;
		}
	if(prev == nil){
		obj->objtab[h] = o;
		return;
	}
	prev->next = o;
}

static OBJObject *
geto(OBJ *obj, char *n)
{
	OBJObject *o;
	uint h;

	h = hash(n);
	for(o = obj->objtab[h]; o != nil; o = o->next)
		if(strcmp(o->name, n) == 0)
			break;
	return o;
}

static void
freetex(OBJTexture *t)
{
	if(t == nil)
		return;
	free(t->filename);
	freememimage(t->image);
}

static OBJMaterial *
allocmt(char *name)
{
	OBJMaterial *m;

	m = emalloc(sizeof *m);
	memset(m, 0, sizeof *m);
	m->name = estrdup(name);
	return m;
}

static void
freemt(OBJMaterial *m)
{
	freetex(m->norm);
	freetex(m->map_Ks);
	freetex(m->map_Kd);
	free(m->name);
	free(m);
}

static OBJMaterlist *
allocmtl(char *file)
{
	OBJMaterlist *ml;

	ml = emalloc(sizeof *ml);
	memset(ml, 0, sizeof *ml);
	ml->filename = estrdup(file);
	return ml;
}

static void
addmtl(OBJMaterlist *ml, OBJMaterial *m)
{
	OBJMaterial *mp, *prev;
	uint h;

	prev = nil;
	h = hash(m->name);
	for(mp = ml->mattab[h]; mp != nil; prev = mp, mp = mp->next)
		if(strcmp(mp->name, m->name) == 0){
			m->next = mp->next;
			freemt(mp);
			break;
		}
	if(prev == nil){
		ml->mattab[h] = m;
		return;
	}
	prev->next = m;
}

static OBJMaterial *
getmtl(OBJMaterlist *ml, char *name)
{
	OBJMaterial *m;
	uint h;

	h = hash(name);
	for(m = ml->mattab[h]; m != nil; m = m->next)
		if(strcmp(m->name, name) == 0)
			return m;
	return nil;
}

OBJMaterlist *
objmtlparse(char *file)
{
	Line curline;
	OBJMaterlist *ml;
	OBJMaterial *m;
	Biobuf *bin;
	char *line, *f[10], *p, wdir[128], buf[128];
	int nf;

	bin = Bopen(file, OREAD);
	if(bin == nil){
		werrstr("Bopen: %r");
		return nil;
	}

	if((p = strrchr(file, '/')) != nil){
		*p++ = 0;
		snprint(wdir, sizeof wdir, "%s", file);
		file = p;
	}else{
		wdir[0] = '.';
		wdir[1] = 0;
	}

	ml = allocmtl(file);
	m = nil;
	curline.file = file;
	curline.line = 0;

	while((line = Brdline(bin, '\n')) != nil){
		curline.line++;
		line[Blinelen(bin)-1] = 0;

		nf = tokenize(line, f, nelem(f));
		if(nf < 1)
			continue;

		if(strcmp(f[0], "newmtl") == 0){
			if(nf != 2){
				error(&curline, "syntax error");
				goto error;
			}
			m = allocmt(f[1]);
			addmtl(ml, m);
		}else if(strcmp(f[0], "Ka") == 0){
			if(nf != 2 && nf != 4){
				error(&curline, "syntax error");
				goto error;
			}
			if(m == nil){
				error(&curline, "no material found");
				goto error;
			}
			if(nf == 2)
				m->Ka.r = m->Ka.g = m->Ka.b = strtod(f[1], nil);
			else{
				m->Ka.r = strtod(f[1], nil);
				m->Ka.g = strtod(f[2], nil);
				m->Ka.b = strtod(f[3], nil);
			}
			m->Ka.a = 1;
		}else if(strcmp(f[0], "Kd") == 0){
			if(nf != 2 && nf != 4){
				error(&curline, "syntax error");
				goto error;
			}
			if(m == nil){
				error(&curline, "no material found");
				goto error;
			}
			if(nf == 2)
				m->Kd.r = m->Kd.g = m->Kd.b = strtod(f[1], nil);
			else{
				m->Kd.r = strtod(f[1], nil);
				m->Kd.g = strtod(f[2], nil);
				m->Kd.b = strtod(f[3], nil);
			}
			m->Kd.a = 1;
		}else if(strcmp(f[0], "Ks") == 0){
			if(nf != 2 && nf != 4){
				error(&curline, "syntax error");
				goto error;
			}
			if(m == nil){
				error(&curline, "no material found");
				goto error;
			}
			if(nf == 2)
				m->Ks.r = m->Ks.g = m->Ks.b = strtod(f[1], nil);
			else{
				m->Ks.r = strtod(f[1], nil);
				m->Ks.g = strtod(f[2], nil);
				m->Ks.b = strtod(f[3], nil);
			}
			m->Ks.a = 1;
		}else if(strcmp(f[0], "Ke") == 0){
			if(nf != 2 && nf != 4){
				error(&curline, "syntax error");
				goto error;
			}
			if(m == nil){
				error(&curline, "no material found");
				goto error;
			}
			if(nf == 2)
				m->Ke.r = m->Ke.g = m->Ke.b = strtod(f[1], nil);
			else{
				m->Ke.r = strtod(f[1], nil);
				m->Ke.g = strtod(f[2], nil);
				m->Ke.b = strtod(f[3], nil);
			}
			m->Ke.a = 1;
		}else if(strcmp(f[0], "Ns") == 0){
			if(nf != 2){
				error(&curline, "syntax error");
				goto error;
			}
			if(m == nil){
				error(&curline, "no material found");
				goto error;
			}
			m->Ns = strtod(f[1], nil);
		}else if(strcmp(f[0], "Ni") == 0){
			if(nf != 2){
				error(&curline, "syntax error");
				goto error;
			}
			if(m == nil){
				error(&curline, "no material found");
				goto error;
			}
			m->Ni = strtod(f[1], nil);
		}else if(strcmp(f[0], "d") == 0){
			if(nf != 2){
				error(&curline, "syntax error");
				goto error;
			}
			if(m == nil){
				error(&curline, "no material found");
				goto error;
			}
			m->d = strtod(f[1], nil);
		}else if(strcmp(f[0], "map_Kd") == 0){
			if(nf != 2){
				error(&curline, "syntax error");
				goto error;
			}
			if(m == nil){
				error(&curline, "no material found");
				goto error;
			}
			snprint(buf, sizeof buf, "%s/%s", wdir, f[1]);
			if((m->map_Kd = readtexturefile(buf)) == nil){
				error(&curline, "readtexturefile: %r");
				goto error;
			}
		}else if(strcmp(f[0], "map_Ks") == 0){
			if(nf != 2){
				error(&curline, "syntax error");
				goto error;
			}
			if(m == nil){
				error(&curline, "no material found");
				goto error;
			}
			snprint(buf, sizeof buf, "%s/%s", wdir, f[1]);
			if((m->map_Ks = readtexturefile(buf)) == nil){
				error(&curline, "readtexturefile: %r");
				goto error;
			}
		}else if(strcmp(f[0], "norm") == 0){
			if(nf != 2){
				error(&curline, "syntax error");
				goto error;
			}
			if(m == nil){
				error(&curline, "no material found");
				goto error;
			}
			snprint(buf, sizeof buf, "%s/%s", wdir, f[1]);
			if((m->norm = readtexturefile(buf)) == nil){
				error(&curline, "readtexturefile: %r");
				goto error;
			}
		}else if(strcmp(f[0], "illum") == 0){
			if(nf != 2){
				error(&curline, "syntax error");
				goto error;
			}
			if(m == nil){
				error(&curline, "no material found");
				goto error;
			}
			m->illum = strtol(f[1], nil, 10);
		}
	}
	Bterm(bin);
	return ml;
error:
	objmtlfree(ml);
	Bterm(bin);
	return nil;
}

void
objmtlfree(OBJMaterlist *ml)
{
	OBJMaterial *m, *nm;
	int i;

	if(ml == nil)
		return;
	for(i = 0; i < nelem(ml->mattab); i++)
		for(m = ml->mattab[i]; m != nil; m = nm){
			nm = m->next;
			freemt(m);
		}
	free(ml->filename);
	free(ml);
}

OBJ *
objparse(char *file)
{
	Biobuf *bin;
	Line curline;
	OBJ *obj;
	OBJObject *o;
	OBJMaterial *m;
	OBJElem *e;
	OBJVertex v;
	double *d;
	char c, wdir[128], buf[256], *p;
	int vtype, idxtab, idx, sign;

	m = nil;
	o = nil;
	bin = Bopen(file, OREAD);
	if(bin == nil)
		sysfatal("Bopen: %r");
	if((p = strrchr(file, '/')) != nil){
		*p++ = 0;
		snprint(wdir, sizeof wdir, "%s", file);
		file = p;
	}else{
		wdir[0] = '.';
		wdir[1] = 0;
	}
	curline.file = file;
	curline.line = 1;
	obj = emalloc(sizeof(OBJ));
	while((c = Bgetc(bin)) != Beof){
		switch(c){
		case 'v':
			d = (double*)&v;
			c = Bgetc(bin);
			vtype = OBJVGeometric;
			switch(c){
			case 't': vtype = OBJVTexture; break;
			case 'p': vtype = OBJVParametric; break;
			case 'n': vtype = OBJVNormal; break;
			default:
				if(!isspace(c)){
					error(&curline, "wrong vertex type");
					goto error;
				}
			}
			while(c = Bgetc(bin), c != Beof && c != '\n' && d-(double*)&v < 4){
				while(isspace(c))
					c = Bgetc(bin);
				if(c == '\\'){
					while(c != '\n')
						c = Bgetc(bin);
					continue;
				}
				if(c != '-' && !isdigit(c)){
					error(&curline, "unexpected character '%c'", c);
					goto error;
				}
				Bungetc(bin);
				Bgetd(bin, d++);
			}
			switch(vtype){
			case OBJVGeometric:
				if(d-(double*)&v < 3){
					error(&curline, "not enough coordinates");
					goto error;
				}
				if(d-(double*)&v < 4)
					*d = 1;	/* default w value */
				break;
			case OBJVTexture:
				if(d-(double*)&v < 1){
					error(&curline, "not enough coordinates");
					goto error;
				}
				while(d-(double*)&v < 3)
					*d++ = 0;	/* default v and w values */
				break;
			case OBJVParametric:
				if(d-(double*)&v < 2){
					error(&curline, "not enough coordinates");
					goto error;
				}
				if(d-(double*)&v < 3)
					*d = 1;	/* default w value */
				break;
			case OBJVNormal:
				if(d-(double*)&v < 3){
					error(&curline, "not enough coordinates");
					goto error;
				}
			}
			addvert(obj, v, vtype);
			break;
		case 'o':
			p = buf;
			c = Bgetc(bin);
			if(!isspace(c)){
				error(&curline, "syntax error");
				goto error;
			}
			while(isspace(c))
				c = Bgetc(bin);
			if(!isalnum(c)){
				error(&curline, "unexpected character '%c'", c);
				goto error;
			}
			do{
				*p++ = c;
			}while(c = Bgetc(bin), isobjname(c) && p-buf < sizeof(buf)-1);
			*p = 0;
			o = geto(obj, buf);
			if(o == nil){
				o = alloco(buf);
				pusho(obj, o);
			}
			break;
		case 'g':
		case 's':
			/* element and smoothing groups ignored for now */
			while(c != '\n')
				c = Bgetc(bin);
			break;
		case 'p':
			c = Bgetc(bin);
			if(!isspace(c)){
				error(&curline, "syntax error");
				goto error;
			}
			while(c = Bgetc(bin), c != '\n'){
				idx = 0;
				sign = 0;
				while(isspace(c))
					c = Bgetc(bin);
				if(c == '\\'){
					while(c != '\n')
						c = Bgetc(bin);
					continue;
				}
				if(c != '-' && !isdigit(c)){
					error(&curline, "unexpected character '%c'", c);
					goto error;
				}
				if(c == '-'){
					sign = 1;
					c = Bgetc(bin);
					if(!isdigit(c)){
						error(&curline, "unexpected character '%c'", c);
						goto error;
					}
				}
				do{
					idx = idx*10 + c-'0';
				}while(c = Bgetc(bin), isdigit(c));
				Bungetc(bin);
				idx = sign ? obj->vertdata[OBJVGeometric].nvert-idx : idx-1;
				if(idx+1 > obj->vertdata[OBJVGeometric].nvert){
					error(&curline, "not enough vertices");
					goto error;
				}
				e = allocelem(OBJEPoint);
				addelemidx(e, OBJVGeometric, idx);
				if(o == nil){
					o = alloco("default");
					pusho(obj, o);
				}
				if(m != nil)
					e->mtl = m;
				addelem(o, e);
			}
			break;
		case 'l':
			c = Bgetc(bin);
			if(!isspace(c)){
				error(&curline, "syntax error");
				goto error;
			}
			while(c = Bgetc(bin), c != '\n'){
				idx = 0;
				sign = 0;
				while(isspace(c))
					c = Bgetc(bin);
				if(c == '\\'){
					while(c != '\n')
						c = Bgetc(bin);
					continue;
				}
				if(c != '-' && !isdigit(c)){
					error(&curline, "unexpected character '%c'", c);
					goto error;
				}
				if(c == '-'){
					sign = 1;
					c = Bgetc(bin);
					if(!isdigit(c)){
						error(&curline, "unexpected character '%c'", c);
						goto error;
					}
				}
				do{
					idx = idx*10 + c-'0';
				}while(c = Bgetc(bin), isdigit(c));
				idx = sign ? obj->vertdata[OBJVGeometric].nvert-idx : idx-1;
				if(idx+1 > obj->vertdata[OBJVGeometric].nvert){
					error(&curline, "not enough vertices");
					goto error;
				}
				e = allocelem(OBJELine);
				addelemidx(e, OBJVGeometric, idx);
Line2:
				idx = 0;
				sign = 0;
				while(isspace(c))
					c = Bgetc(bin);
				if(c == '\\'){
					while(c != '\n')
						c = Bgetc(bin);
					c = Bgetc(bin);
					goto Line2;
				}
				if(c != '-' && !isdigit(c)){
					freeelem(e);
					error(&curline, "unexpected character '%c'", c);
					goto error;
				}
				if(c == '-'){
					sign = 1;
					c = Bgetc(bin);
					if(!isdigit(c)){
						freeelem(e);
						error(&curline, "unexpected character '%c'", c);
						goto error;
					}
				}
				do{
					idx = idx*10 + c-'0';
				}while(c = Bgetc(bin), isdigit(c));
				Bungetc(bin);
				idx = sign ? obj->vertdata[OBJVGeometric].nvert-idx : idx-1;
				if(idx+1 > obj->vertdata[OBJVGeometric].nvert){
					freeelem(e);
					error(&curline, "not enough vertices");
					goto error;
				}
				addelemidx(e, OBJVGeometric, idx);
				if(o == nil){
					o = alloco("default");
					pusho(obj, o);
				}
				if(m != nil)
					e->mtl = m;
				addelem(o, e);
			}
			break;
		case 'f':
			e = allocelem(OBJEFace);
			idxtab = 0;
			c = Bgetc(bin);
			if(!isspace(c)){
				freeelem(e);
				error(&curline, "syntax error");
				goto error;
			}
			while(c = Bgetc(bin), c != '\n'){
				idx = 0;
				sign = 0;
				if(isspace(c))
					idxtab = 0;
				while(isspace(c))
					c = Bgetc(bin);
				if(c == '\\'){
					while(c != '\n')
						c = Bgetc(bin);
					continue;
				}
				if(c == '/'){
					if(++idxtab >= OBJNVERT){
						freeelem(e);
						error(&curline, "unknown vertex type '%d'", idxtab);
						goto error;
					}
					continue;
				}
				if(c != '-' && !isdigit(c)){
					freeelem(e);
					error(&curline, "unexpected character '%c'", c);
					goto error;
				}
				if(c == '-'){
					sign = 1;
					c = Bgetc(bin);
					if(!isdigit(c)){
						freeelem(e);
						error(&curline, "unexpected character '%c'", c);
						goto error;
					}
				}
				do{
					idx = idx*10 + c-'0';
				}while(c = Bgetc(bin), isdigit(c));
				Bungetc(bin);
				idx = sign ? obj->vertdata[idxtab].nvert-idx : idx-1;
				if(idx+1 > obj->vertdata[idxtab].nvert){
					freeelem(e);
					error(&curline, "not enough vertices");
					goto error;
				}
				addelemidx(e, idxtab, idx);
			}
			if(o == nil){
				o = alloco("default");
				pusho(obj, o);
			}
			if(m != nil)
				e->mtl = m;
			addelem(o, e);
			break;
		case 'm':
		case 'u':
			p = buf;
			do{
				*p++ = c;
			}while(c = Bgetc(bin), isalpha(c) && p-buf < sizeof(buf)-1);
			*p = 0;
			if(strcmp(buf, "mtllib") == 0){
				while(isspace(c))
					c = Bgetc(bin);
				p = seprint(buf, buf + sizeof buf, "%s/", wdir);
				do{
					*p++ = c;
				}while(c = Bgetc(bin), isapath(c) && p-buf < sizeof(buf)-1);
				*p = 0;
				if((obj->materials = objmtlparse(buf)) == nil)
					fprint(2, "warning: objmtlparse: %r\n");
			}else if(strcmp(buf, "usemtl") == 0){
				while(isspace(c))
					c = Bgetc(bin);
				p = buf;
				do{
					*p++ = c;
				}while(c = Bgetc(bin), ismtlname(c) && p-buf < sizeof(buf)-1);
				*p = 0;
				if(obj->materials == nil || (m = getmtl(obj->materials, buf)) == nil)
					fprint(2, "warning: no material '%s' found\n", buf);
			}else{
				error(&curline, "syntax error");
				goto error;
			}
			while(c != '\n')
				c = Bgetc(bin);
			break;
		case '#':
			while(c != '\n')
				c = Bgetc(bin);
			break;
		}
		do{
			if(c == '\n'){
				curline.line++;
				break;
			}
			if(!isspace(c)){
				error(&curline, "syntax error");
				goto error;
			}
		}while((c = Bgetc(bin)) != Beof);
	}
	Bterm(bin);
	return obj;
error:
	objfree(obj);
	Bterm(bin);
	return nil;
}

void
objfree(OBJ *obj)
{
	OBJObject *o, *no;
	int i;

	if(obj == nil)
		return;
	if(obj->materials != nil)
		objmtlfree(obj->materials);
	for(i = 0; i < nelem(obj->vertdata); i++)
		free(obj->vertdata[i].verts);
	for(i = 0; i < nelem(obj->objtab); i++)
		for(o = obj->objtab[i]; o != nil; o = no){
			no = o->next;
			freeo(o);
		}
	free(obj);
}

int
objexport(char *path, OBJ *obj)
{
	Fmt f;
	OBJMaterial *m;
	char fmtbuf[8192], buf[256], *bufe, *pe;
	va_list va;
	int fd, i;

	bufe = buf + sizeof buf;
	pe = seprint(buf, bufe, "%s", path);

	seprint(pe, bufe, "/main.obj");
	fd = create(buf, OWRITE|OEXCL, 0644);
	if(fd < 0){
		werrstr("create: %r");
		return -1;
	}
	fmtfdinit(&f, fd, fmtbuf, sizeof fmtbuf);
	/* NOTE i don't like this either, but i don't want to mess with the fmt table */
	va = (va_list)&obj;
	f.args = va;
	OBJfmt(&f);
	fmtfdflush(&f);
	close(fd);

	if(obj->materials == nil)
		return 0;

	seprint(pe, bufe, "/%s", obj->materials->filename);
	fd = create(buf, OWRITE|OEXCL, 0644);
	if(fd < 0){
		werrstr("create: %r");
		return -1;
	}
	fmtfdinit(&f, fd, fmtbuf, sizeof fmtbuf);
	va = (va_list)&obj->materials;
	f.args = va;
	OBJMaterlistfmt(&f);
	fmtfdflush(&f);
	close(fd);

	for(i = 0; i < nelem(obj->materials->mattab); i++)
		for(m = obj->materials->mattab[i]; m != nil; m = m->next){
			if(m->map_Kd != nil){
				seprint(pe, bufe, "/%s", m->map_Kd->filename);
				if(writeimagefile(buf, m->map_Kd->image) < 0)
					fprint(2, "writeimagefile: %r");
			}
			if(m->map_Ks != nil){
				seprint(pe, bufe, "/%s", m->map_Ks->filename);
				if(writeimagefile(buf, m->map_Ks->image) < 0)
					fprint(2, "writeimagefile: %r");
			}
			if(m->norm != nil){
				seprint(pe, bufe, "/%s", m->norm->filename);
				if(writeimagefile(buf, m->norm->image) < 0)
					fprint(2, "writeimagefile: %r");
			}
		}

	return 0;
}

int
OBJMaterlistfmt(Fmt *f)
{
	OBJMaterlist *ml;
	OBJMaterial *m;
	int n, i;

	n = 0;
	ml = va_arg(f->args, OBJMaterlist*);

	for(i = 0; i < nelem(ml->mattab); i++)
		for(m = ml->mattab[i]; m != nil; m = m->next){
			n += fmtprint(f, "newmtl %s\n", m->name);
			if(memcmp(&m->Ka, &ZC, sizeof(ZC)) != 0)
				n += fmtprint(f, "Ka %g %g %g\n", m->Ka.r, m->Ka.g, m->Ka.b);
			if(memcmp(&m->Kd, &ZC, sizeof(ZC)) != 0)
				n += fmtprint(f, "Kd %g %g %g\n", m->Kd.r, m->Kd.g, m->Kd.b);
			if(memcmp(&m->Ks, &ZC, sizeof(ZC)) != 0)
				n += fmtprint(f, "Ks %g %g %g\n", m->Ks.r, m->Ks.g, m->Ks.b);
			if(memcmp(&m->Ke, &ZC, sizeof(ZC)) != 0)
				n += fmtprint(f, "Ke %g %g %g\n", m->Ke.r, m->Ke.g, m->Ke.b);
			if(m->Ns != 0)
				n += fmtprint(f, "Ns %g\n", m->Ns);
			if(m->Ni != 0)
				n += fmtprint(f, "Ni %g\n", m->Ni);
			if(m->d != 0)
				n += fmtprint(f, "d %g\n", m->d);
			if(m->illum != 0)
				n += fmtprint(f, "illum %d\n", m->illum);
			if(m->map_Kd != nil)
				n += fmtprint(f, "map_Kd %s\n", m->map_Kd->filename);
			if(m->map_Ks != nil)
				n += fmtprint(f, "map_Ks %s\n", m->map_Ks->filename);
			if(m->norm != nil)
				n += fmtprint(f, "norm %s\n", m->norm->filename);
			n += fmtprint(f, "\n");
		}

	return n;
}

int
OBJfmt(Fmt *f)
{
	OBJ *obj;
	OBJObject *o;
	OBJElem *e;
	OBJVertex v;
	OBJMaterial *curmtl;
	int i, j, k, n, pack, maxnindex;

	n = pack = 0;
	curmtl = nil;
	obj = va_arg(f->args, OBJ*);

	if(obj->materials != nil)
		n += fmtprint(f, "mtllib %s\n", obj->materials->filename);

	for(i = 0; i < nelem(obj->vertdata); i++)
		for(j = 0; j < obj->vertdata[i].nvert; j++){
			v = obj->vertdata[i].verts[j];
			switch(i){
			case OBJVGeometric:
				n += fmtprint(f, "v %g %g %g %g\n", v.x, v.y, v.z, v.w);
				break;
			case OBJVTexture:
				n += fmtprint(f, "vt %g %g %g\n", v.u, v.v, v.vv);
				break;
			case OBJVNormal:
				n += fmtprint(f, "vn %g %g %g\n", v.i, v.j, v.k);
				break;
			case OBJVParametric:
				n += fmtprint(f, "vp %g %g %g\n", v.u, v.v, v.vv);
				break;
			}
		}

	for(i = 0; i < nelem(obj->objtab); i++)
		for(o = obj->objtab[i]; o != nil; o = o->next){
			if(strcmp(o->name, "default") != 0)
				n += fmtprint(f, "o %s\n", o->name);
			for(e = o->child; e != nil; e = e->next){
				switch(e->type){
				case OBJEPoint:
					if(pack == 0)
						n += fmtprint(f, "p");
					pack = pack > 0 ? --pack : 8-1;
					break;
				case OBJELine:
					n += fmtprint(f, "l");
					break;
				case OBJEFace:
					if(e->mtl != nil && e->mtl != curmtl){
						n += fmtprint(f, "usemtl %s\n", e->mtl->name);
						curmtl = e->mtl;
					}
					n += fmtprint(f, "f");
					break;
				//case OBJECurve:
				//case OBJECurve2:
				//case OBJESurface:
				}
				for(maxnindex = 0, j = 0; j < nelem(e->indextab); j++)
					maxnindex = max(e->indextab[j].nindex, maxnindex);
				for(k = 0; k < maxnindex; k++){
					n += fmtprint(f, " ");
					for(j = 0; j < nelem(e->indextab); j++){
						if(k >= e->indextab[j].nindex)
							continue;
						if(j > 0)
							n += fmtprint(f, "/");
						n += fmtprint(f, "%d", e->indextab[j].indices[k]+1);
					}
				}
				if(e->type != OBJEPoint || pack == 0)
					n += fmtprint(f, "\n");
			}
		}

	return n;
}

void
OBJfmtinstall(void)
{
	fmtinstall('O', OBJfmt);
	fmtinstall('M', OBJMaterlistfmt);
}

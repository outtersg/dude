#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/param.h>
#include <stdio.h>
#include <dirent.h>
#include <stdarg.h>
#include <sys/mman.h>

/* Cet utilitaire est destiné à tourner sur un Mac OS X 10.8; les optimisations du style linkat, fdopendir, sont donc remises à plus tard. */

int g_realiser;

/*- Basiques -----------------------------------------------------------------*/

void err(char * quoi, ...)
{
	va_list args;
	va_start(args, quoi);
	fprintf(stderr, "# ");
	vfprintf(stderr, quoi, args);
	fprintf(stderr, "\n");
	va_end(args);
}

#define TROUVER(trouve, pos, quoi, Type, champ, entrees, nEntrees) \
	do \
	{ \
		int _d = 0; \
		int _f = nEntrees; \
		while(_f > _d) \
			if(entrees[pos = (_d + _f) / 2].champ == quoi) \
				break; \
			else if(entrees[pos].champ < quoi) \
				_d = pos + 1; \
			else \
				_f = pos; \
		if(!(trouve = (_f > _d))) \
			pos = _d; \
	} \
	while(0)

#define PLACERN(Type, entrees, nEntrees, nEntreesAllouees, pos, tailleAlloc) \
	do \
	{ \
		if(nEntrees >= nEntreesAllouees) \
		{ \
			nEntreesAllouees += nEntreesAllouees > tailleAlloc * 16 ? nEntreesAllouees / 16 : tailleAlloc; \
			Type * entrees2 = (Type *)malloc(nEntreesAllouees * sizeof(Type)); \
			if(entrees) \
			{ \
			memcpy(entrees2, entrees, pos * sizeof(Type)); \
			free(entrees); \
			} \
			entrees = entrees2; \
		} \
		if(nEntrees > pos) \
			memcpy(&entrees[pos + 1], &entrees[pos], (nEntrees - pos) * sizeof(Type)); \
		++nEntrees; \
	} \
	while(0);

#define PLACER(Type, entrees, nEntrees, nEntreesAllouees, pos) PLACERN(Type, entrees, nEntrees, nEntreesAllouees, pos, 64)

#define TROUVEROUCREER(trouve, pos, quoi, Type, champ, entrees, nEntrees, nEntreesAllouees, init) \
	do \
	{ \
		TROUVER(trouve, pos, quoi, Type, champ, entrees, nEntrees); \
		if(!trouve) \
		{ \
			PLACER(Type, entrees, nEntrees, nEntreesAllouees, pos); \
			entrees[pos].champ = quoi; \
			init; \
		} \
	} \
	while(0);

/*- Somme --------------------------------------------------------------------*/

#include "crc32.c"
typedef uint32_t crc_t;

void crcFichier(int fd, size_t taille, crc_t * ptrCrc)
{
	void * mem = taille ? mmap(NULL, taille, PROT_READ, MAP_PRIVATE, fd, 0) : NULL;
	if(mem == MAP_FAILED)
	{
		err("mmap a échoué: %s", strerror(errno));
		exit(1);
	}
	#if 0
	*ptrCrc = 0; /* À FAIRE: n'y a-t-il pas une valeur de départ pour les crc32? */
	*ptrCrc = calculate_crc32c(*ptrCrc, mem, taille);
	#else
	crc32mem(mem, taille, ptrCrc);
	#endif
	if(mem)
		munmap(mem, taille);
}

/*- Chemin -------------------------------------------------------------------*/

typedef struct Chemin
{
	char * c;
	struct Chemin * p;
} Chemin;

Chemin * CheminNouveau(Chemin * pere, const char * chemin)
{
	Chemin * n = (Chemin *)malloc(sizeof(Chemin));
	n->p = pere;
	n->c = (char *)malloc(strlen(chemin));
	strcpy(n->c, chemin);
	return n;
}

static char tempCheminComplet[MAXPATHLEN];

char * CheminComplet(Chemin * chemin, char * chaineChemin)
{
	Chemin * ptr;
	int tailleChemin;
	int tailleSegment;
	
	if(!chaineChemin) chaineChemin = tempCheminComplet;
	
	for(ptr = chemin, tailleChemin = -1; ptr; ptr = ptr->p)
		tailleChemin += 1 + strlen(ptr->c);
	chaineChemin[tailleChemin] = 0;
	for(ptr = chemin; ptr;)
	{
		tailleChemin -= (tailleSegment = strlen(ptr->c));
		memcpy(&chaineChemin[tailleChemin], ptr->c, tailleSegment);
		if((ptr = ptr->p))
			chaineChemin[--tailleChemin] = '/';
	}
	return chaineChemin;
}

int crcChemin(Chemin * chemin, int taille, crc_t * ptrCrc)
{
	int f;
	
	f = open(CheminComplet(chemin, NULL), O_RDONLY);
	if(f < 0) { err("Ouverture de %s impossible: %s", CheminComplet(chemin, NULL), strerror(errno)); return 0; }
	crcFichier(f, taille, ptrCrc);
	close(f);
	
	return 1;
}

/*- Inodes -------------------------------------------------------------------*/

typedef struct CorrInode
{
	ino_t inode;
	Chemin * chemin;
} CorrInode;

/*- Racine -------------------------------------------------------------------*/

typedef struct Racine
{
	Chemin * cheminActuel;
	CorrInode * inodes;
	int nInodes;
	int nInodesAlloues;
} Racine;

void RacineInit(Racine * racine, const char * chemin)
{
	racine->cheminActuel = CheminNouveau(NULL, chemin);
	
	racine->inodes = NULL;
	racine->nInodes = 0;
	racine->nInodesAlloues = 0;
}

Chemin * RacineInode(struct Racine * racine, uint32_t inode)
{
	int pos;
	int trouve;
	TROUVER(trouve, pos, inode, CorrInode, inode, racine->inodes, racine->nInodes);
	
	return NULL;
}

/*- Boulot -------------------------------------------------------------------*/

void analyser(Racine * racine, struct dirent * f)
{
	RacineInode(racine, f);
}


void parcourir(Racine * racine, char * nom)
{
	Chemin * cheminActuel = racine->cheminActuel;
	
	int tailleNom = strlen(nom);
	Chemin * monChemin;
	if(tailleNom > 0 && nom[0] == '/')
	{
		Chemin * monChemin = CheminNouveau(NULL, nom);
		racine->cheminActuel = monChemin;
	}
	else
	{
		Chemin * monChemin = CheminNouveau(racine->cheminActuel, nom);
		racine->cheminActuel = monChemin;
	}
	
	DIR * dossier;
	struct dirent * entree;
	
	dossier = opendir(nom);
	if(!dossier)
	{
		err("Le dossier %s a disparu au moment d'y accéder", nom);
		goto e0;
	}
	fchdir(dirfd(dossier));
	while((entree = readdir(dossier)))
	{
		if(0 == strcmp(".", entree->d_name) || 0 == strcmp("..", entree->d_name))
			continue;
		if(entree->d_type == DT_DIR)
		{
			parcourir(racine, entree->d_name);
			fchdir(dirfd(dossier));
		}
		else if(entree->d_type == DT_REG)
			analyser(racine, entree);
	}
	closedir(dossier);
	
e0:
	racine->cheminActuel = cheminActuel;
}

int main(int argc, char ** argv)
{
	/* On pousse par défaut en sens inverse. Le dernier argument est considéré comme la référence (à analyser en premier, donc), afin que sur du 2016-*, le dernier en date soit considéré comme la référence. */
	
	int sens = -1;
	int i;
	int nFichiers;
	char ** fichiers = (char **)malloc(argc * sizeof(char *)); /* Mieux vaut réserver trop que pas assez. */
	Racine racine;
	RacineInit(& racine, ".");
	DIR * ici = opendir(".");
	int fdIci = dirfd(ici);
	
	g_realiser = 1;
	for(i = 0; ++i < argc;)
		if(0 == strcmp(argv[i], "-r"))
			sens = -sens;
		else if(0 == strcmp(argv[i], "-n"))
			g_realiser = 0;
		else if(sens > 0)
			fichiers[nFichiers++] = argv[i];
		else
		{
			memmove(&fichiers[1], fichiers, (argc - 1) * sizeof(char *));
			fichiers[0] = argv[i];
			++nFichiers;
		}
	for(i = 0; i < nFichiers; ++i)
	{
		fchdir(fdIci);
		parcourir(& racine, fichiers[i]);
	}
	
	closedir(ici);
}

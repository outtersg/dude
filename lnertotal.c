#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <stdio.h>
#include <dirent.h>
#include <stdarg.h>

/* Cet utilitaire est destiné à tourner sur un Mac OS X 10.8; les optimisations du style linkat, fdopendir, sont donc remises à plus tard. */

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

/*- Inodes -------------------------------------------------------------------*/

typedef struct CorrInode
{
	uint32_t inode;
	Chemin * chemin;
} CorrInode;

/*- Racine -------------------------------------------------------------------*/

typedef struct Racine
{
	Chemin * cheminActuel;
	char chaineCheminActuel[MAXPATHLEN + 1];
	CorrInode * inodes;
	int nInodes;
	int nInodesAlloues;
} Racine;

void RacineInit(Racine * racine, const char * chemin)
{
	int tailleChemin = strlen(chemin);
	while(tailleChemin > 1 && chemin[tailleChemin - 1] == '/')
		--tailleChemin;
	strcpy(&racine->chaineCheminActuel[0], chemin);
	racine->chaineCheminActuel[tailleChemin] = 0;
	racine->cheminActuel = CheminNouveau(NULL, racine->chaineCheminActuel);
	racine->chaineCheminActuel[tailleChemin] = '/';
	racine->chaineCheminActuel[tailleChemin + 1] = 0;
	
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

void analyser(Racine * racine, char * nom, uint32_t inode)
{
	RacineInode(racine, inode);
}


void parcourir(Racine * racine, char * nom)
{
	int tailleChaineCheminActuel = strlen(&racine->chaineCheminActuel[0]);
	Chemin * cheminActuel = racine->cheminActuel;
	char * chaineCheminActuelComplete = NULL;
	
	int tailleNom = strlen(nom);
	Chemin * monChemin;
	if(tailleNom > 0 && nom[0] == '/')
	{
		Chemin * monChemin = CheminNouveau(NULL, nom);
		racine->cheminActuel = monChemin;
		chaineCheminActuelComplete = (char *)alloca(tailleChaineCheminActuel + 1);
		strcpy(chaineCheminActuelComplete, racine->chaineCheminActuel);
		strcpy(racine->chaineCheminActuel, nom);
		racine->chaineCheminActuel[tailleNom] = '/';
		racine->chaineCheminActuel[tailleNom + 1] = 0;
	}
	else
	{
		Chemin * monChemin = CheminNouveau(racine->cheminActuel, nom);
		racine->cheminActuel = monChemin;
		strcpy(&racine->chaineCheminActuel[tailleChaineCheminActuel], nom);
		racine->chaineCheminActuel[tailleChaineCheminActuel + tailleNom] = '/';
		racine->chaineCheminActuel[tailleChaineCheminActuel + tailleNom + 1] = 0;
	}
	
	DIR * dossier;
	struct dirent * entree;
	
	dossier = opendir(racine->chaineCheminActuel);
	while((entree = readdir(dossier)))
	{
		if(0 == strcmp(".", entree->d_name) || 0 == strcmp("..", entree->d_name))
			continue;
		if(entree->d_type == DT_DIR)
			parcourir(racine, entree->d_name);
		else if(entree->d_type == DT_REG)
			analyser(racine, entree->d_name, entree->d_fileno);
	}
	closedir(dossier);
	
	racine->cheminActuel = cheminActuel;
	if(chaineCheminActuelComplete)
		strcpy(racine->chaineCheminActuel, chaineCheminActuelComplete);
	else
		racine->chaineCheminActuel[tailleChaineCheminActuel] = 0;
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
	
	for(i = 0; ++i < argc;)
		if(0 == strcmp(argv[i], "-r"))
			sens = -sens;
		else if(sens > 0)
			fichiers[nFichiers++] = argv[i];
		else
		{
			memmove(&fichiers[1], fichiers, (argc - 1) * sizeof(char *));
			fichiers[0] = argv[i];
			++nFichiers;
		}
	for(i = 0; i < nFichiers; ++i)
		parcourir(& racine, fichiers[i]);
}

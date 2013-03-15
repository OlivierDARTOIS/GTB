// Fonctions de gestion du panneau DEL

#include "panneaudel.h"

using namespace std;

unsigned int 	fd_cs, fd_clk, fd_dat;
char texte[100];
unsigned char pos_texte=0;
unsigned char Screen[48] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

void initialisationGPIOPanneau()
{
    gpio_export(CS);
    gpio_export(CLK);
    gpio_export(DAT);
    gpio_set_dir(CS, 1); 		// 1: sortie  0:entrée
    gpio_set_dir(CLK, 1); 		// 1: sortie  0:entrée
    gpio_set_dir(DAT, 1); 		// 1: sortie  0:entrée
    gpio_set_value(CS,1);       // Mise à 1 des sorties
    gpio_set_value(CLK,1);
    gpio_set_value(DAT,1);
    fd_cs  = gpio_fd_open(CS);  // Ouverture des "broches"
    fd_clk = gpio_fd_open(CLK); // pas de gestion d'erreur ici
    fd_dat = gpio_fd_open(DAT);
}

void fermetureGPIOPanneau()
{
    gpio_fd_close(fd_cs);
    gpio_fd_close(fd_clk);
    gpio_fd_close(fd_dat);
}

void envoiCommandeInitialisationPanneau(unsigned short donneesCommande)
{
    unsigned char i;                            // entier non signé codé sur 8 bits
    unsigned short j;                           // entier non signé codé sur 16 bits

    donneesCommande=donneesCommande&0x0fff;     // On ne garde que les 12 bits de poids faible
    donneesCommande=donneesCommande<<4;         // On décale de 4 rang vers la gauche pour
                                                // avoir les 12 bits précédents sur les poids fort

    write(fd_cs, "0", 2);                       // Ligne CS du HT1632 à 0 : HT1632 activé

    for(i=0;i<12;i++)                           // On doit envoyer les 12 bits précédents
    {
        write(fd_clk, "0", 2);                  // Ligne WR (ici SCLK) à 0
        j=donneesCommande&0x8000;               // on récupère uniquement le bit de poids fort
        donneesCommande=donneesCommande<<1;     // on décale d'un rang vers la gauche pour le prochain bit
        j=j>>15;                                // on décale de 15 rang vers la droite pour mettre
                                                // le bit sur lequel on travaille en bit de poids faible
        if (j)                                  // En fonction de l'état de ce bit (0 ou 1)
            write(fd_dat, "1", 2);              // on positionne la ligne DATA à 1
        else                                    // ou
            write(fd_dat, "0", 2);              // on positionne la ligne DATA à 0

        write(fd_clk, "1", 2);                  // On valide la ligne DATA sur le front montant sur la ligne WR
    }
    write(fd_cs, "1", 2);                       // Ligne CS du panneau passe à 1 => panneau désactivé
    write(fd_dat, "1", 2);                      // On positionne la ligne DATA à 1 (état de repos)
}

void initialisationPanneau()
{
    // D'apres doc officiel :
    // sequence init: SYS DIS,COMMONS OPTION,MASTER MODE,SYS EN,LED ON, PWM DUTY
    envoiCommandeInitialisationPanneau(0b100000000000); // SYS DIS
    envoiCommandeInitialisationPanneau(0b100001010000); // COMMONS OPTION : P-MOS open drain and 8 common
    envoiCommandeInitialisationPanneau(0b100000101000); // MASTER MODE
    envoiCommandeInitialisationPanneau(0b100000000010); // SYS EN
    envoiCommandeInitialisationPanneau(0b100000000110); // LED ON
    envoiCommandeInitialisationPanneau(0b100101011110); // PWM DUTY : 16/16
}

void rafraichissementPanneau()
{
    unsigned char i,j,temp;

    write(fd_cs, "0", 2);

    write(fd_clk, "0", 2);
    write(fd_dat, "1", 2);        // Envoi d'un '1'
    write(fd_clk, "1", 2);

    write(fd_clk, "0", 2);
    write(fd_dat, "0", 2);        // Envoi d'un '0'
    write(fd_clk, "1", 2);

    write(fd_clk, "0", 2);
    write(fd_dat, "1", 2);        // Envoi d'un '1'
    write(fd_clk, "1", 2);

    for(i=0; i<7; i++)     // Envoi de l'adresse, ici à 000000 car on réactualise tout le panneau
    {
        write(fd_clk, "0", 2);
        write(fd_dat, "0", 2);
        write(fd_clk, "1", 2);
    }

    for (j=0;j<32;j++)   // Envoi des données de l'écran : tableau 'Screen'
    {
        temp = Screen[j];
        for(i=0; i<8; i++)
        {
            write(fd_clk, "0", 2);
            if (temp & 0x01) write(fd_dat, "1", 2); else write(fd_dat, "0", 2);
            write(fd_clk, "1", 2);
            temp=temp>>1;
        }
    }

    write(fd_cs, "1", 2);
    write(fd_dat, "1", 2);
}

// Programme de test XBee avec thread
// compilation avec la ligne suivante:
// g++ -W -Wall -std=c++0x -o XBee_Thread main.cpp serialib.cpp noeud.cpp -lpthread
//
// Un excellent PDF pour la programmation Unix et des threads en particulier
// www1.geexbox.org/~ben/book-screen.pdf


#include <string>
#include <vector>
#include <thread>
#include <time.h>
#include "serialib.h"
#include "panneaudel.h"

using namespace std;

void* receptionTrame(void* parametre);
void* analyseTrame(void* parametre);
void* envoyerMessage(void* parametre);
void* gestionPanneau(void* parametre);

struct donneesThread
{
    // Attention a bien mettre un espace entre les deux chevrons a la
    // fin de la declaration de vecteurs de vecteurs si la version
    // du compilateur ne gere pas C++ 11
    vector <vector <unsigned char> > trameZigBeeReception;
    vector <string> messageAEnvoyer;
    string messageAAfficher;
    serialib VS;
};

pthread_mutex_t mutexTrameZigBee = PTHREAD_MUTEX_INITIALIZER ;

int main()
{
    donneesThread* donneesTrame = new donneesThread;
    pthread_t threadRecepTrame;
    pthread_t threadAnalyseTrame;
    pthread_t threadEnvoyerMessage;
    pthread_t threadGestionPanneau;

    int Ret = donneesTrame->VS.Open("/dev/ttySP1",9600);
    if (Ret != 1) {
        cout << "Erreur a l'ouverture du port serie." << endl;
        return Ret;
    }

    cout << "Main : Lancement des Threads" << endl;
    pthread_create(&threadRecepTrame, NULL, receptionTrame, reinterpret_cast<void*>(donneesTrame));
    pthread_create(&threadAnalyseTrame, NULL, analyseTrame, reinterpret_cast<void*>(donneesTrame));
    pthread_create(&threadEnvoyerMessage, NULL, envoyerMessage, reinterpret_cast<void*>(donneesTrame));
    pthread_create(&threadGestionPanneau, NULL, gestionPanneau, reinterpret_cast<void*>(donneesTrame));

    // On boucle tant que l'on a pas attendu 90s
    int cptSec=0;
    do {
        cout << "Main : Nombre de secondes : "<< cptSec << "s" << endl;
        sleep(1);
        cptSec++;
    } while (cptSec < 90);

    // Force la fin des thread
    pthread_cancel(threadRecepTrame);
    pthread_cancel(threadAnalyseTrame);
    pthread_cancel(threadEnvoyerMessage);
    pthread_cancel(threadGestionPanneau);

    // Libère les ressources du thread
    pthread_join(threadRecepTrame, NULL);
    pthread_join(threadAnalyseTrame, NULL);
    pthread_join(threadEnvoyerMessage, NULL);
    pthread_join(threadGestionPanneau, NULL);

    // Affichage du vecteur de trames ZigBee pour debug
    /*
    for (unsigned char i=0;i<donneesTrame->trameZigBeeReception.size();i++) {
        cout << "TZB " << (int)i << ": ";
        for(unsigned char j=0;j<donneesTrame->trameZigBeeReception[i].size();j++) {
            cout << "0x" << hex << (int)donneesTrame->trameZigBeeReception[i][j] << dec << " ";
        }
        cout << endl;
    }
    */

    donneesTrame->VS.Close();
    delete donneesTrame;

    return 0;
}

// Thread de reception de la voie serie et stockage des trames dans un vecteur
void* receptionTrame(void* parametre)
{
    cout << "TRVS : Dans Thread reception voie serie (TRVS)" << endl;
    donneesThread* pData = reinterpret_cast<donneesThread*>(parametre);

    vector <unsigned char> donneesReceptionVoieSerie;
    char car;
    do {
        // Lecture bloquante d'un caractère sur la voie série
        do {
            cout << "TRVS : En attente caractere debut de trame 7E" << endl;
            pData->VS.ReadChar(&car,0);
        } while (car != 0x7E);

        cout << "TRVS : caractere 7E recu" << endl;

        // Attention il se peut aussi que la longeur de la trame soit échapper
        int cpt=0;
        unsigned int lg=0;
        do {
            pData->VS.ReadChar(&car,0);
            if (car == 0x7D) { pData->VS.ReadChar(&car,0); car = car ^ 0x20; }
            if (cpt == 0) lg = car * 256;
            if (cpt == 1) lg += car;
            cpt++;
        } while (cpt < 2);
        cout << "TRVS : Longueur de la trame : 0x" << hex << lg << dec << endl;

        // Reception de la trame
        do {
            pData->VS.ReadChar(&car,0);
            if (car == 0x7D) { pData->VS.ReadChar(&car,0); car = car ^ 0x20; }
            donneesReceptionVoieSerie.push_back(car);
        } while (donneesReceptionVoieSerie.size() != (lg+1)); // (lg+1) => permet de récupérer le CRC même si celui-ci est echappé
        cout << "TRVS : Reception de 0x" << hex << donneesReceptionVoieSerie.size() << dec << " octets (avec CRC)" << endl;

        // Verification du CRC
        cout << "TRVS : Calcul et verification du CRC" << endl;
        unsigned char crc_calc=0;
        for (unsigned int i=0; i<=lg; i++) crc_calc += donneesReceptionVoieSerie[i];
        cout << "TRVS : CRC calcule : 0x" << hex << (int)crc_calc << dec;
        if (crc_calc == 0xFF) {
            cout << " OK" << endl;
            cout << "TRVS : Ajout de la trame dans le vecteur sans CRC" << endl;
            donneesReceptionVoieSerie.pop_back();
            // *** DEBUT ZONE CRITIQUE ***
            pthread_mutex_lock(&mutexTrameZigBee);
            pData->trameZigBeeReception.push_back(donneesReceptionVoieSerie);
            pthread_mutex_unlock(&mutexTrameZigBee);
            // *** FIN ZONE CRITIQUE ***
            // Ne pas oublier d'effacer le vecteur temporaire de réception des caractères de la voie serie
            donneesReceptionVoieSerie.clear();
        }
        else {
            cout << " Erreur !" << endl;
            cout << "TRVS : Elimination de la trame erronee" << endl;
        }
    } while (true);

    cout << "TRVS : Fin Thread reception voie serie (TRVS)" << endl;
    pthread_exit(NULL);
    return NULL;
}

// Thread d'analyse des trames ZigBee recues
void* analyseTrame(void* parametre)
{
    cout << "TATZ : Dans Thread analyse trame zigbee (TATZ)" << endl;
    donneesThread* pData = reinterpret_cast<donneesThread*>(parametre);

    vector <unsigned char> trameZigBeeAAnalyser;

    do {
        // On ira verifier s'il y a des trames toutes les 10ms
        usleep(10000);
        // Y a t il des trames ZigBee à analyser
        if (pData->trameZigBeeReception.size() != 0) {
            trameZigBeeAAnalyser = pData->trameZigBeeReception[0];
            cout << "TATZ : Tentative de suppression trame ZigBee" << endl;
            // *** DEBUT ZONE CRITIQUE ***
            pthread_mutex_lock(&mutexTrameZigBee);
            pData->trameZigBeeReception.erase(pData->trameZigBeeReception.begin());
            pthread_mutex_unlock(&mutexTrameZigBee);
            // *** FIN ZONE CRITIQUE ***
            cout << "TATZ : Suppression trame ZigBee effective" << endl;

            // En fonction de la trame recue on adapte le traitement
            string messageAEnvoyer;
            bool reponseAttendue=false;

            switch (trameZigBeeAAnalyser[0]) {
            case 0x90 : // Trame ZigBee Receive Packet
                cout << "TATZ : Donnees ASCII recues : ";
                for (unsigned char i=12; i<trameZigBeeAAnalyser.size(); i++) cout << (char)trameZigBeeAAnalyser[i] << dec << " ";
                cout << endl;
                switch (trameZigBeeAAnalyser[12]) {
                case 'T' : // le coordinateur demande la temperature
                    messageAEnvoyer = "T+24";
                    reponseAttendue = true;
                    break;
                case 'F' : // le coordinateur demande l'etat du capteur de fumees/gaz
                    messageAEnvoyer = "F0";
                    reponseAttendue = true;
                    break;
                case 'I' : // le coordinateur demande l'etat du capteur de presence
                    messageAEnvoyer = "I1";
                    reponseAttendue = true;
                    break;
                case 'S' : // le coordinateur demande l'activation de la sonnerie
                    cout << "TATZ : Activation sonnerie" << endl;
                    break;
                case 'A' : // le coordinateur demande l'activation de l'alarme
                    cout << "TATZ : Activation alarme" << endl;
                    break;
                case 'D' : // le coordinateur transmet le date et l'heure
                    cout << "TATZ : Reglage date/heure" << endl;
                    break;
                case 'M' :  // le coordinateur transmet un message a afficher
                    cout << "TATZ : Message a afficher : ";
                    for (unsigned char i=13; i<trameZigBeeAAnalyser.size(); i++) cout << (char)trameZigBeeAAnalyser[i];
                    cout << endl;
                    // *** DEBUT ZONE CRITIQUE ***
                    pthread_mutex_lock(&mutexTrameZigBee);
                    for (unsigned char i=13; i<trameZigBeeAAnalyser.size(); i++)
                       //pData->messageAAfficher[i-13] = (char)trameZigBeeAAnalyser[i]; // Ne fonctionne pas !
                        pData->messageAAfficher += (char)trameZigBeeAAnalyser[i];
                    pthread_mutex_unlock(&mutexTrameZigBee);
                    // *** FIN ZONE CRITIQUE ***
                    break;
                default : // commande inconnu
                        cout << "TATZ : Commande inconnue " << endl;
                }

                // *** DEBUT ZONE CRITIQUE ***
                if (reponseAttendue) {
                    pthread_mutex_lock(&mutexTrameZigBee);
                    pData->messageAEnvoyer.push_back(messageAEnvoyer);
                    pthread_mutex_unlock(&mutexTrameZigBee);
                }
                // *** FIN ZONE CRITIQUE ***
                break;
            default :  // Trame non traitée ou inconnue
                    cout << "TATZ : Trame ZigBee non traitees ou inconnues" << endl;
            }
        }
    } while(true);

    cout << "TATZ : Fin Thread analyse trames zigbee (TATZ)" << endl;
    pthread_exit(NULL);
    return NULL;
}

// Thread envoi des messages ASCII
void* envoyerMessage(void* parametre)
{
    cout << "TEMA : Dans Thread envoi des messages ASCII (TEMA)" << endl;
    donneesThread* pData = reinterpret_cast<donneesThread*>(parametre);

    string messageAEnvoyer;
    unsigned char trame_non_echap[128] = {
        0x7E,			// SOF
        0x00, 0x00,		// longueur du champ de données (sans 7E, sans Lg, sans CRC)
        0x10,               	// Envoie d'une trame ZigBee
        0x00,			// 0 : sans acquitement du recepteur, 1 : avec acquitement
        0x00, 0x00, 0x00, 0x00,	// Addr haute du destinataire (32 bits)
        0x00, 0x00, 0x00, 0x00, // Addr basse du destinataire (32 bits)
        0xFF, 0xFE,		// Addr courte du destinataire sur 16 bits, ici FFFE = inconnu
        0x00,			// Nb max de saut lors d'un broadcast : 0=infini
        0x00			// champ option, non utilisé ici donc a mettre à 0
                                // les données doivent être positionné ici
    };

    unsigned char trame_echap[128];

    do {
        // On ira verifier s'il y a des messages à envoyer toutes les 10ms
        usleep(10000);
        // Y a t il des messages à envoyer
        if (pData->messageAEnvoyer.size() != 0) {
            messageAEnvoyer = pData->messageAEnvoyer[0];
            cout << "TEMA : Tentative de suppression message" << endl;
            // *** DEBUT ZONE CRITIQUE ***
            pthread_mutex_lock(&mutexTrameZigBee);
            pData->messageAEnvoyer.erase(pData->messageAEnvoyer.begin());
            pthread_mutex_unlock(&mutexTrameZigBee);
            // *** FIN ZONE CRITIQUE ***
            cout << "TEMA : Suppression message effective" << endl;

            cout << "TEMA : Message envoye : " << messageAEnvoyer << endl;

            // remplissage du champ longueur
            unsigned short lg=0;
            lg = 14 + static_cast<unsigned short>(messageAEnvoyer.size());      // 14 = nb d'octets avant les donnees et sans le SOF et les deux octets de Lg
            trame_non_echap[1] = (unsigned char)((lg&0xFF00)>>8);               // MSB en premier
            trame_non_echap[2] = (unsigned char)(lg&0x00FF);                    // LSB en second

            // Remplissage des données
            for (unsigned int i=0; i<messageAEnvoyer.size(); i++) trame_non_echap[17+i]=(unsigned char)(messageAEnvoyer[i]);

            // calcul du crc
            // Addition en mode 8 bit de tous les octets depuis 0x10 jusqu'à la fin des data
            unsigned char crc=0;
            for (int i=0;i<lg;i++) crc += trame_non_echap[i+3];
            crc = 0xFF - crc;

            trame_non_echap[lg+3] = crc;

            // Generation de la trame pour le mode API2
            // Les caractères à échapper sont:
            // Le début de trame (SOF) 0x7E sauf bien sur le premier qui indique le début d'une trame
            // Pour détecter le début d'une trame ZigBee, il suffit donc d'attendre ce caractère
            // le caractère d'échappement : 0x7D
            // les caractères qui gèrent le flux logiciellement: XON (0x11) et XOFF (0x13)
            // Pour échapper un caractère : mettre 0x7D puis le caractère concerné XOR 0x20
            trame_echap[0] = trame_non_echap[0]; 	// Le SOF (0x7E) n'est jamais echappé
            int j=1;
            for (int i=1;i<lg+4;i++) {
                switch (trame_non_echap[i]) {
                case 0x7E:
                case 0x7D:
                case 0x11:
                case 0x13:
                    trame_echap[j++] = 0x7D;
                    trame_echap[j++] = trame_non_echap[i] ^ 0x20;
                    break;
                default:
                    trame_echap[j++] = trame_non_echap[i];
                }
            }

            // Envoi de la trame vers le module XBee concerné
            cout << "TEMA : envoi de la trame ZigBee : ";
            for (unsigned char i=0;i<j;i++) cout << hex << (int)trame_echap[i] << " " << dec;
            cout << endl;
            for (unsigned char i=0;i<j;i++) pData->VS.WriteChar(trame_echap[i]);
        }
    } while(true);

    cout << "TEMA : Fin Thread envoi des messages ASCII (TEMA)" << endl;
    pthread_exit(NULL);
    return NULL;
}

// Thread de gestion du panneau DEL
void* gestionPanneau(void* parametre)
{
    cout << "TGPD : Dans Thread Gestion Panneau Dels (TGPD)" << endl;
    donneesThread* pData = reinterpret_cast<donneesThread*>(parametre);

    unsigned char i=0,j=0,cpt;
    char* temp;
    time_t heureBrute;
    struct tm* heure;

    initialisationGPIOPanneau();
    initialisationPanneau();

    do {
        time(&heureBrute);
        heure = localtime(&heureBrute);
        temp = asctime(heure);

        if (pData->messageAAfficher.size()!=0) {
            pthread_mutex_lock(&mutexTrameZigBee);
            cout << "TGPD : Message a afficher : " << pData->messageAAfficher << endl;
            //snprintf(texte,100,"%s",pData->messageAAfficher.c_str());
            memset(Screen,0,48);
            strcpy(texte,pData->messageAAfficher.c_str());
            pData->messageAAfficher.clear();
            pthread_mutex_unlock(&mutexTrameZigBee);
            if (strlen(texte)<7) {
                for (cpt=0;cpt<strlen(texte);cpt++) {
                    Screen[cpt*5]   = font_5x7[(unsigned short)(texte[cpt]-32)*5];
                    Screen[cpt*5+1] = font_5x7[(unsigned short)(texte[cpt]-32)*5+1];
                    Screen[cpt*5+2] = font_5x7[(unsigned short)(texte[cpt]-32)*5+2];
                    Screen[cpt*5+3] = font_5x7[(unsigned short)(texte[cpt]-32)*5+3];
                    Screen[cpt*5+4] = font_5x7[(unsigned short)(texte[cpt]-32)*5+4];
                }
                rafraichissementPanneau();
                sleep(5);
            } else {
                memset(Screen,0,48);
                cpt=0;
                strcat(texte,"       ");
                do {
                    rafraichissementPanneau();
                    for (int i=0;i<48;i++) { Screen[i]=Screen[i+1]; }
                    j++;
                    if (j==7) {
                        j=0;
                        Screen[40] = font_5x7[(unsigned short)(texte[cpt]-32)*5];
                        Screen[41] = font_5x7[(unsigned short)(texte[cpt]-32)*5+1];
                        Screen[42] = font_5x7[(unsigned short)(texte[cpt]-32)*5+2];
                        Screen[43] = font_5x7[(unsigned short)(texte[cpt]-32)*5+3];
                        Screen[44] = font_5x7[(unsigned short)(texte[cpt]-32)*5+4];
                        cpt++;
                    }
                    usleep(30000);
                } while (cpt < strlen(texte));
            }
        } else {
            if (i%2)
                sprintf(texte," %c%c %c%c ", temp[11],temp[12],temp[14],temp[15]);
            else
                sprintf(texte," %c%c:%c%c ", temp[11],temp[12],temp[14],temp[15]);
            for (cpt=0;cpt<strlen(texte);cpt++) {
                Screen[cpt*5]   = font_5x7[(unsigned short)(texte[cpt]-32)*5];
                Screen[cpt*5+1] = font_5x7[(unsigned short)(texte[cpt]-32)*5+1];
                Screen[cpt*5+2] = font_5x7[(unsigned short)(texte[cpt]-32)*5+2];
                Screen[cpt*5+3] = font_5x7[(unsigned short)(texte[cpt]-32)*5+3];
                Screen[cpt*5+4] = font_5x7[(unsigned short)(texte[cpt]-32)*5+4];
            }
            rafraichissementPanneau();
            i++;
            usleep(500000);
        }    
    } while(true);

    cout << "TGPD : Fin Thread Gestion Panneau Dels (TGPD)" << endl;
    pthread_exit(NULL);
    return NULL;
}

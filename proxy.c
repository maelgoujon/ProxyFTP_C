// GOUJON Mael, DARDET Lenny
// Proxy FTP entre un client Actif et un serveur FTP Passif
// compatible avec les commandes : USER, PASS, QUIT, PORT, FEAT
// Repond à ls pwd et cd
// testé avec vsftpd
// 2024

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include "./simpleSocketAPI.h"

#define SERVADDR "0.0.0.0"         // Définition de l'adresse IP d'écoute
#define SERVPORT "0"               // Définition du port d'écoute, si 0 port choisi dynamiquement
#define LISTENLEN 5                // Taille de la file des demandes de connexion
#define MAXBUFFERLEN 1024          // Taille du tampon pour les échanges de données
#define MAXBUFFERDATALEN 8192      // Taille du tampon pour les échanges des connections data
#define MAXHOSTLEN 64              // Taille d'un nom de machine
#define MAXPORTLEN 64              // Taille d'un numéro de port
int ecode = 0;                     // Code retour des fonctions
char bufferData[MAXBUFFERDATALEN]; // Tampon de communication entre le client et le serveur

void sendClient(int sock, char *buffer);
void sendServer(int sock, char *buffer);
void readClient(int sock, char *buffer);
void readServer(int sock, char *buffer);
void readDataConnection(int sock, char *buffer, char *bufferData);
void closeConnections(int sockRemoteServer, int descSockCOM);

int main()
{
    char serverAddr[MAXHOSTLEN];    // Adresse du serveur
    char serverPort[MAXPORTLEN];    // Port du server
    int descSockRDV;                // Descripteur de socket de rendez-vous/ d'écoute serveur
    int descSockCOM;                // Descripteur de socket de communication client
    int descSockDATACOM;            // Descripteur de socket de communication data
    struct addrinfo hints;          // Contrôle la fonction getaddrinfo
    struct addrinfo *res;           // Contient le résultat de la fonction getaddrinfo
    struct sockaddr_storage myinfo; // Informations sur la connexion de RDV
    struct sockaddr_storage from;   // Informations sur le client connecté
    socklen_t len;                  // Variable utilisée pour stocker les
                                    // longueurs des structures de socket
    char buffer[MAXBUFFERLEN];      // Tampon de communication entre le client et le serveur
    char bufferCut[5];              // Tampon prenant les 4 premiers caractères du buffer (les commandes ftp)
    // Information serveur distant
    int sockRemoteServer;
    int sockRemoteDataServer;
    char login[30];
    char password[30];
    char serveurAdresse[MAXHOSTLEN];
    char remoteServerPort[MAXPORTLEN] = "21";
    bool remoteConnectionOk = true;
    bool sendData = false;
    // Initialisation de la socket de RDV IPv4/TCP
    descSockRDV = socket(AF_INET, SOCK_STREAM, 0);

    if (descSockRDV == -1)
    {
        perror("Erreur création socket RDV\n");
        exit(2);
    }
    // Publication de la socket au niveau du système
    // Assignation d'une adresse IP et un numéro de port
    // Mise à zéro de hints
    memset(&hints, 0, sizeof(hints));
    // Initialisation de hints
    hints.ai_flags = AI_PASSIVE;     // mode serveur, nous allons utiliser la fonction bind
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_family = AF_INET;       // seules les adresses IPv4 seront présentées par
                                     // la fonction getaddrinfo
    // Récupération des informations du serveur
    ecode = getaddrinfo(SERVADDR, SERVPORT, &hints, &res);
    if (ecode)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ecode));
        exit(1);
    }
    // Publication de la socket
    ecode = bind(descSockRDV, res->ai_addr, res->ai_addrlen);
    if (ecode == -1)
    {
        perror("Erreur liaison de la socket de RDV");
        exit(3);
    }
    // Nous n'avons plus besoin de cette liste chainée addrinfo
    freeaddrinfo(res);
    // Récuppération du nom de la machine et du numéro de port pour affichage à l'écran
    len = sizeof(struct sockaddr_storage);
    ecode = getsockname(descSockRDV, (struct sockaddr *)&myinfo, &len);
    if (ecode == -1)
    {
        perror("SERVEUR: getsockname");
        exit(4);
    }
    ecode = getnameinfo((struct sockaddr *)&myinfo, sizeof(myinfo), serverAddr, MAXHOSTLEN,
                        serverPort, MAXPORTLEN, NI_NUMERICHOST | NI_NUMERICSERV);
    if (ecode != 0)
    {
        fprintf(stderr, "error in getnameinfo: %s\n", gai_strerror(ecode));
        exit(4);
    }
    printf("L'adresse d'ecoute est: %s\n", serverAddr);
    printf("Le port d'ecoute est: %s\n", serverPort);
    // Definition de la taille du tampon contenant les demandes de connexion
    ecode = listen(descSockRDV, LISTENLEN);
    if (ecode == -1)
    {
        perror("Erreur initialisation buffer d'écoute");
        exit(5);
    }
    len = sizeof(struct sockaddr_storage);
    while (1)
    {
        // Attente connexion du client
        // Lorsque demande de connexion, creation d'une socket de communication avec le client
        descSockCOM = accept(descSockRDV, (struct sockaddr *)&from, &len);
        if (descSockCOM == -1)
        {
            perror("Erreur accept\n");
            exit(6);
        }
        // à chaque nouveau client, on fork() pour avoir plusieurs clients simultanés
        pid_t child_pid = fork();
        // Si erreur fork()
        if (child_pid == -1)
        {
            perror("Fils non créé\n");
            exit(EXIT_FAILURE);
        }
        // Si processus fils ok
        if (child_pid == 0)
        {
            // Demande de login@serveur au client
            strcpy(buffer, "220 Connection ex: anonymous@ftp.fau.de\n");
            sendClient(descSockCOM, buffer);

            // Tant que l'utilisateur est connecté
            while (remoteConnectionOk)
            {
                readClient(descSockCOM, buffer);
                // On récupère le login et le serveur
                sscanf(buffer, "%[^@]@%s", login, serveurAdresse);

                // On retourne une erreur au client si le login ou le serveur est vide
                if (strcmp(login, "") == 0 || strcmp(serveurAdresse, "") == 0)
                {
                    // Message d'erreur pour le client
                    strcpy(buffer, "530 Login incorrect.\r\n");
                    printf("Login incorrect : %s\n", buffer);
                    sendClient(descSockCOM, buffer);
                    // Fermeture de la connexion
                    strcpy(buffer, "221 Bye.\r\n");
                    sendClient(descSockCOM, buffer);
                    printf("Fermeture connexion serveur .\n");

                    close(sockRemoteServer);
                    close(descSockCOM);
                    close(descSockRDV);

                    // Fermeture du processus fils
                    exit(0);
                }

                printf("Utilisateur : %s\n", login);
                printf("Serveur : %s\n", serveurAdresse);
                // Connection au serveur FTP
                ecode = connect2Server(serveurAdresse, remoteServerPort, &sockRemoteServer);
                if (ecode == -1)
                {
                    perror("Err");
                    exit(8);
                }
                printf("Connecté à %s:%s\n", serveurAdresse, remoteServerPort);
                // On lit le message du serveur
                readServer(sockRemoteServer, buffer);
                printf("Message du serveur : %s\n", buffer);
                bool userConnectedToRemote = false;
                // Tant que l'utilisateur est connecté au serveur
                while (remoteConnectionOk)
                {
                    // Si l'utilisateur n'est pas encore connecté au serveur alors on le connecte
                    if (!userConnectedToRemote)
                    {
                        printf("On connecte l'utilisateur au serveur.\n");
                        // On envoi le login au serveur distant
                        strcat(login, "\r\n");
                        write(sockRemoteServer, login, strlen(login));
                        printf("Login envoyé : %s\n", login);
                        // 230 Utilisateur connecté
                        readServer(sockRemoteServer, buffer);
                        printf("Message du serveur : %s\n", buffer);
                        // On transmet le message du serveur au client
                        sendClient(descSockCOM, buffer);
                        printf("Message du serveur transmis au client : %s\n", buffer);
                        userConnectedToRemote = true;
                    }
                    //"En attente" d'une commande du client
                    printf("En attente d'une commande du client...\n");
                    printf("-----------------------------------\n");
                    readClient(descSockCOM, buffer);
                    printf("Commande du client : %s\n", buffer);
                    // Si la commande est QUIT alors on ferme la connexion avec le serveur
                    strncpy(bufferCut, buffer, 4);
                    if (strcmp(bufferCut, "QUIT") == 0)
                    {
                        printf("Commande QUIT recue.\n");
                        remoteConnectionOk = false;
                        memset(buffer, 0, MAXBUFFERLEN);
                        strcpy(buffer, "221 Bye.\r\n");
                        sendClient(descSockCOM, buffer);
                    }
                    // Si la commande est PORT
                    else if (strcmp(bufferCut, "PORT") == 0)
                    {
                        printf("Commande PORT recue.\n");
                        // On récupère ip et port client
                        printf("Récupération de l'ip et du port client.\n");
                        char ip1[4], ip2[4], ip3[4], ip4[4], firstPort[4], secondPort[4];
                        // On découpe le buffer pour récupérer les 4 octets de l'ip et les 2 octets du port séparés par une virgule
                        sscanf(buffer, "PORT %[^,],%[^,],%[^,],%[^,],%[^,],%s", ip1, ip2, ip3, ip4, firstPort, secondPort);
                        char ipClientData[16];
                        // On concatène les 4 octets de l'ip dans ipClientData
                        sprintf(ipClientData, "%s.%s.%s.%s", ip1, ip2, ip3, ip4);
                        // On calcule le port
                        int port = atol(firstPort) * 256 + atol(secondPort);
                        char portClientString[6];
                        // On converti le port en string
                        sprintf(portClientString, "%d", port);
                        printf("ipClientData : %s\n", ipClientData);
                        printf("portClientData : %s\n", portClientString);
                        // On envoi PORT OK au client
                        memset(buffer, 0, MAXBUFFERLEN);
                        strcpy(buffer, "200 Commande PORT OK.\r\n");
                        sendClient(descSockCOM, buffer);
                        // Mode passif entre proxy et serveur
                        printf("Passage en mode passif coté proxy - serveur distant FTP.\n");
                        memset(buffer, 0, MAXBUFFERLEN);
                        strcpy(buffer, "PASV\r\n");
                        sendServer(sockRemoteServer, buffer);
                        // message du serveur
                        readServer(sockRemoteServer, buffer);
                        // On récupère ip et port serveur
                        printf("Récupération de l'ip et du port serveur distant FTP.\n");
                        // 227 pour le mode passif
                        // On met les 4 octets de l'ip et les 2 octets du port dans des variables
                        sscanf(buffer, "227 Entering Passive Mode (%[^,],%[^,],%[^,],%[^,],%[^,],%[^)]).", ip1, ip2, ip3, ip4, firstPort, secondPort);
                        char ipFtpDistantData[16];
                        // On concatène les 4 octets de l'ip dans ipFtpDistantData
                        sprintf(ipFtpDistantData, "%s.%s.%s.%s", ip1, ip2, ip3, ip4); // On calcule le port
                        port = atol(firstPort) * 256 + atol(secondPort);
                        char portServeurDataString[6];
                        // On converti le port en string
                        sprintf(portServeurDataString, "%d", port);
                        // On se connecte au serveur en mode passif
                        ecode = connect2Server(ipFtpDistantData, portServeurDataString, &sockRemoteDataServer);
                        if (ecode == -1)
                        {
                            perror("Erreur connexion serveur");
                            exit(8);
                        }
                        printf("Serveur mode PASSIF OK\n");

                        // On récupère la commande du client
                        readClient(descSockCOM, buffer);
                        printf("Commande du client : %s\n", buffer);
                        // On envoi la commande au serveur
                        sendServer(sockRemoteServer, buffer);
                        printf("Commande envoyée au serveur distant FTP : %s\n", buffer);
                        // retour serveur contenant le ls
                        readServer(sockRemoteServer, buffer);
                        // On envoi le message au client
                        sendClient(descSockCOM, buffer);
                        // On lit le message du serveur
                        readDataConnection(sockRemoteDataServer, buffer, bufferData);
                        close(sockRemoteDataServer);
                        // Connection en mode actif au client
                        ecode = connect2Server(ipClientData, portClientString, &descSockDATACOM);
                        if (ecode == -1)
                        {
                            perror("Erreur connexion client");
                            exit(8);
                        }
                        sendClient(descSockDATACOM, bufferData);
                        close(descSockDATACOM);
                        memset(bufferData, 0, MAXBUFFERDATALEN);
                        readServer(sockRemoteServer, buffer);
                        sendClient(descSockCOM, buffer);
                    }
                    // commandes supportées par le serveur (FEAT)
                    else if (strcmp(bufferCut, "FEAT") == 0)
                    {
                        printf("Commande FEAT recue.\n");

                        // Envoie la commande FEAT au serveur distant
                        sendServer(sockRemoteServer, "FEAT\r\n");
                        printf("Commande FEAT envoyée au serveur.\n");

                        // Pour attendre 211 End, des fois ca prend du temps et le programme continue -> erreur
                        while (1)
                        {
                            // Lit la réponse du serveur distant ligne par ligne
                            readServer(sockRemoteServer, buffer);
                            printf("Message du serveur : %s\n", buffer);

                            // Transmet la ligne au client
                            sendClient(descSockCOM, buffer);
                            printf("Message du serveur transmis au client.\n");

                            // Si la ligne lue se termine par "End", la réponse est complète
                            if (strstr(buffer, "End") != NULL)
                            {
                                break;
                            }
                        }
                    }

                    // Il arrive que le client envoie EPRT au lieu de PORT, vsftp ne supporte pas EPRT
                    else if (strcmp(bufferCut, "EPRT") == 0)
                    {
                        strcpy(buffer, "500 Commande EPRT non supportée. Utilisez plutot PORT\r\n");
                        printf("Commande non supportée : %s\n", buffer);
                        sendClient(descSockCOM, buffer);
                    }
                    else
                    {
                        printf("Commande non reconnue transmise au serveur: %s\n", buffer);
                        // Pour tout les autres messages
                        //  On transmet le message du client au serveur
                        sendServer(sockRemoteServer, buffer);

                        // On lit le message du serveur
                        readServer(sockRemoteServer, buffer);
                        printf("Message du serveur : %s\n", buffer);

                        // On transmet le message au client
                        sendClient(descSockCOM, buffer);
                    }
                }
            }
            // Fermeture de la connexion
            close(sockRemoteServer);
            printf("Fermeture connexion serveur .\n");
            close(descSockCOM);
            printf("Fermeture connexion client .\n");
            close(descSockRDV);
            printf("Fermeture socket RDV .\n");
            exit(0);
        }
    }
}
// Fonctions
void sendClient(int sock, char *buffer)
{
    ecode = write(sock, buffer, strlen(buffer));
    if (ecode == -1)
    {
        perror("Erreur envoi message client");
        exit(5);
    }
}
void sendServer(int sock, char *buffer)
{
    ecode = write(sock, buffer, strlen(buffer));
    if (ecode == -1)
    {
        perror("Erreur envoi message serveur");
        exit(5);
    }
}
void readClient(int sock, char *buffer)
{
    memset(buffer, 0, MAXBUFFERLEN);
    ecode = read(sock, buffer, MAXBUFFERLEN - 1);
    buffer[ecode] = '\0';
    if (ecode == -1)
    {
        perror("Erreur lecture socket client");
        exit(7);
    }
}
void readServer(int descSock, char *buffer)
{
    ssize_t bytesRead = read(descSock, buffer, MAXBUFFERLEN - 1);
    if (bytesRead == -1)
    {
        perror("Erreur lors de la lecture depuis le serveur");
        exit(EXIT_FAILURE);
    }

    buffer[bytesRead] = '\0'; // Fin de chaîne
}
void readDataConnection(int sock, char *buffer, char *bufferData)
{
    // Initialise les buffers à zéro
    memset(buffer, 0, MAXBUFFERLEN);
    memset(bufferData, 0, MAXBUFFERDATALEN);
    do{
        // Lit les données depuis la connexion de données et les stocke dans le tampon buffer
        ecode = read(sock, buffer, MAXBUFFERLEN - 1);
        // Vérifie si des données ont été lues
        if (ecode > 0)
        {
            // Concatène les données lues dans le tampon bufferData
            strcat(bufferData, buffer);
            // Réinitialise le tampon buffer
            memset(buffer, 0, MAXBUFFERLEN);
        }
        else if (ecode == -1)
        {
            perror("Erreur lecture socket");
            exit(7);
        }
    } while (ecode > 0);
}

void closeConnections(int sockRemoteServer, int descSockCOM)
{
    // Fermeture de la connexion avec le serveur distant
    close(sockRemoteServer);

    // Fermeture de la connexion avec le client
    close(descSockCOM);
}
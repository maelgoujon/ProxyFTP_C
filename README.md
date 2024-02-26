# Objectif :
Réaliser un proxy qui prend en charge le mode actif côté client et le relai en mode passif au serveur.

# Situation :
Un utilisateur souhaitant établir une session FTP via le proxy s’identifiera en tant que
nomlogin@nomserveur. Le programme client établit une connexion de contrôle avec le
proxy et lui transmet la commande USER nomlogin@nomserveur qui permet au proxy
d’établir une connexion de contrôle sur la machine de nom nomserveur et d’identifier
l’utilisateur à l’aide de nomlogin.

# Conditions :
Les échanges doivent être conformes au protocole FTP.
Plusieurs connexions clients simultanées.
Mode actif côté client et mode passif côté serveur.

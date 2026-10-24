# the one where everyone goes back to the lobby: a --dedicated host ends the
# first mission from its console, the endscreen hands everyone back to the
# hub after ten seconds, the client readies up and the mission starts again.
TEAM_STAGE=0
TEAM_ROLES="c,b"
CLIENTS=1
RUN_SECS=150
HOST_DRIVER=lobbyloop
CLIENT_DRIVER=lobbyloop
EXTRA_HOST_ARGS="--dedicated"
EXPECT_LOOP=1

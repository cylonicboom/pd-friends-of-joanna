# the one where the host is left alone: a --dedicated host starts the mission
# for cl1, cl1 walks out mid-mission, and cl2 knocks afterwards. the host must
# notice it is alone, go back to the hub by itself and let cl2 in - before
# this it sat in the empty stage refusing everyone "team mission in progress".
TEAM_STAGE=0
TEAM_ROLES="c,b"
CLIENTS=1
RUN_SECS=165
CLIENT_DELAY=12
CLIENT1_SECS=45
LATE_CLIENT_AT=95
HOST_DRIVER=readyup
CLIENT_DRIVER=readyup
EXTRA_HOST_ARGS="--dedicated"
EXPECT_REJOIN=1

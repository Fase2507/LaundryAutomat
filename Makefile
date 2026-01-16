PYTHON = python3
DOCKER_COMPOSE = docker compose
DB_SCRIPT = database/database_file.py
WEB_APP=web.py

all:setup run

view:
	@echo "Viewing tables.."
	$(PYTHON) database/view_db.py
setup:
	@echo "Creating tables..."
	$(PYTHON) $(DB_SCRIPT)
	
docker:
	@echo "Docker-compose is running.."
	$(DOCKER_COMPOSE) up -d

run:    docker
	@echo "Starting Flask web app.."
	#$(PYTHON) $(WEB_APP)

clean:
	@echo "Stopping container and cleaning up.."
	$(DOCKER_COMPOSE) down
logs:
	@echo "Showing logs"
	$(DOCKER_COMPOSE) logs -f	

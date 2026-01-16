FROM python:3.12-slim

WORKDIR /app

COPY requirement.txt ./

RUN pip install --no-cache-dir -r requirement.txt

COPY . .

RUN mkdir -p /app/database

EXPOSE 5000

CMD ["python", "web.py"]

import os
from datetime import datetime, timezone, timedelta

import matplotlib.pyplot as plt
from flask import Flask, request, jsonify
from flask_sqlalchemy import SQLAlchemy
from sqlalchemy import func, case, extract, and_, or_
from sqlalchemy.orm import Mapped, mapped_column


app = Flask(name)
app.config["UPLOAD_FOLDER"] = f"{app.root_path}/static/"
app.config["SQLALCHEMY_DATABASE_URI"] = ""
app.config["SQLALCHEMY_ENGINE_OPTIONS"] = {"pool_recycle": 280}

db = SQLAlchemy(app)


class SensorData(db.Model):
    id: Mapped[int] = mapped_column(primary_key=True)
    temperature: Mapped[float] = mapped_column(nullable=False)
    humidity: Mapped[float] = mapped_column(nullable=False)
    recorded_at: Mapped[datetime] = mapped_column(nullable=False)


def generate_plot(x, y_day_data, y_night_data, plot_type, user_id):
    type = "температура" if plot_type == "temperature" else "вологість"
    unit = "°C" if plot_type == "temperature" else "%"

    plt.figure(figsize=(10, 5))
    plt.plot(x, y_day_data, marker="o", linestyle="-", color="b", label=f"Середня {type} вдень")
    plt.plot(x, y_night_data, marker="o", linestyle="-", color="k", label=f"Середня {type} вночі")

    plt.title(f"Середня {type} вдень і вночі за останній тиждень")
    plt.xlabel("Дата")
    plt.ylabel(f"{type.capitalize()}, {unit}")
    plt.grid(True)
    plt.legend()

    file_name = f"{user_id}.png"
    file_path = os.path.join(app.config["UPLOAD_FOLDER"], file_name)
    plt.savefig(file_path)
    plt.close()

    return file_name


@app.post("/save-data")
def save_data():
    data = request.get_json()

    for item in data:
        timestamp = datetime.fromtimestamp(item["timestamp"]).isoformat()
        new_item = SensorData(temperature=item["temperature"], humidity=item["humidity"], recorded_at=timestamp)
        db.session.add(new_item)

    db.session.commit()

    return jsonify({"message": "Data saved successfully"})


@app.get("/generate-plot/<type>/<user_id>")
def generate_plot_by_type(type, user_id):
    if type not in ["temperature", "humidity"]:
        return jsonify({"message": "Invalid plot type. Please specify temperature or humidity."}), 400

    today = datetime.now(timezone.utc)
    one_week_ago = today - timedelta(days=7)

    specify_column = SensorData.temperature if type == "temperature" else SensorData.humidity

    data = db.session.execute(
        db.select(
            func.avg(case((and_(extract("hour", SensorData.recorded_at) >= 6, extract("hour", SensorData.recorded_at) <= 17), specify_column), else_=None)).label("avg_day_data"),
            func.avg(case((or_(extract("hour", SensorData.recorded_at) < 6, extract("hour", SensorData.recorded_at) > 17), specify_column), else_=None)).label("avg_night_data"),
            func.date(SensorData.recorded_at).label("recorded_at"),
        )
        .filter(SensorData.recorded_at >= one_week_ago)
        .group_by(func.date(SensorData.recorded_at))
    ).all()

    if not data:
        return jsonify({"message": "No available for the last week"}), 400

    x_data = [item.recorded_at for item in data]
    y_day_data = [item.avg_day_data for item in data]
    y_night_data = [item.avg_night_data for item in data]

    plot_name = generate_plot(x_data, y_day_data, y_night_data, type, user_id)

    return jsonify({"plot": plot_name})


with app.app_context():
    db.create_all()

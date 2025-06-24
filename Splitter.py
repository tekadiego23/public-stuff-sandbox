import pandas as pd
import requests
import numpy as np

def process_dataframe_weighted(df, base_url="http://localhost:8000"):
    # 1. Calcul du nombre de scénarios
    def get_nb_scenarios(row):
        try:
            from_ = float(row["scenarioFrom"]) if row["scenarioFrom"] else 0
            to = float(row["scenarioTo"]) if row["scenarioTo"] else 0
        except:
            return 1
        return int(to - from_) if to - from_ > 0 else 1

    df["nb_scenarios"] = df.apply(get_nb_scenarios, axis=1)

    # 2. Génération des pv_stat_id
    df["rs_request"] = df.apply(
        lambda row: [
            f"pvstats#var#sophis#{product}#pricing#{row['cob_date']}"
            for product in row["products"]
        ],
        axis=1
    )

    # 3. Ajout d'un identifiant unique par ligne source (si pas de "id")
    df["row_id"] = df.index

    # 4. Explosion
    df_exploded = df.explode("rs_request", ignore_index=True)
    df_exploded.rename(columns={"rs_request": "pv_stat_id"}, inplace=True)

    # 5. Appel API
    unique_requests = df_exploded["pv_stat_id"].unique().tolist()
    try:
        response = requests.post(f"{base_url}/pvstats", json=unique_requests)
        response.raise_for_status()
        rest_response = response.json()
    except requests.RequestException as e:
        raise RuntimeError(f"Erreur API /pvstats : {e}")

    # 6. Mapping de taskPlCalcTime
    df_exploded["taskPlCalcTime"] = df_exploded["pv_stat_id"].map(
        lambda k: rest_response.get(k, {}).get("taskPlCalcTime", np.nan)
    )

    # 7. Calcul du poids pondéré par pv = taskPlCalcTime × nb_scenarios
    df_exploded["weight"] = df_exploded["taskPlCalcTime"] * df_exploded["nb_scenarios"]

    # 8. Somme des poids par tâche (groupée par row_id)
    total_weight_per_task = df_exploded.groupby("row_id")["weight"].transform("sum")

    # 9. Calcul du coût pondéré par pv
    df_exploded["task_cost_split"] = df_exploded["task_cost"] * df_exploded["weight"] / total_weight_per_task

    # 10. Nettoyage final
    return df_exploded.reset_index(drop=True)
